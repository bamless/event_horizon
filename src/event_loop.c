#include "event_loop.h"

#include <jstar/jstar.h>
#include <signal.h>
#include <string.h>

#include "callbacks.h"
#include "errors.h"

static JStarSymbol* sym_loop;
static JStarSymbol* sym_id;
static JStarSymbol* sym_handles;
static JStarSymbol* sym_ref;
static JStarSymbol* sym_get;
static JStarSymbol* sym_unref;

static JStarSymbol* sym_g_addException;
static JStarSymbol* sym_g_clearExceptions;
static JStarSymbol* sym_g_getExceptions;
static JStarSymbol* sym_g_raiseProgramInterrupt;

// class EventLoop
#define G_ADD_EXCEPTION    "_addException"
#define G_CLEAR_EXCEPTIONS "_clearExceptions"
#define G_GET_EXCEPTIONS   "_getExceptions"
#define G_RAISE_INTERRUPT  "_raiseProgramInterrupt"

static void sigintCallback(uv_signal_t* signal, int signum) {
    (void)signum;

    LoopMetadata* metadata = signal->loop->data;
    JStarVM* vm = metadata->vm;

    if(!jsrGetGlobalCached(vm, "event_horizon.uv.event_loop", G_RAISE_INTERRUPT,
                           sym_g_raiseProgramInterrupt) ||
       !jsrCall(vm, 0)) {
        EventLoop_addException(vm, -1);
        jsrPop(vm);
    } else {
        jsrPop(vm);
    }

    uv_stop(signal->loop);
}

static void closeRunSignalHandle(uv_loop_t* loop, uv_signal_t* signal) {
    uv_signal_stop(signal);
    uv_close((uv_handle_t*)signal, NULL);
    uv_run(loop, UV_RUN_NOWAIT);
}

static uv_loop_t* getUVLoopUserdata(JStarVM* vm, int eventLoopSlot) {
    if(!jsrGetFieldCached(vm, eventLoopSlot, M_LOOP_LOOP, sym_loop)) return NULL;
    if(!jsrCheckUserdata(vm, -1, "EventLoop." M_LOOP_LOOP)) return NULL;
    uv_loop_t* loop = jsrGetUserdata(vm, -1);
    jsrPop(vm);
    return loop;
}

static bool setLoopClosed(JStarVM* vm, int eventLoopSlot, bool closed) {
    jsrPushBoolean(vm, closed);
    if(!jsrSetField(vm, eventLoopSlot, M_LOOP_CLOSED)) {
        jsrPop(vm);
        return false;
    }
    jsrPop(vm);
    return true;
}

bool EventLoop_run(JStarVM* vm) {
    JSR_CHECK(Int, 1, "mode");
    int mode = jsrGetNumber(vm, 1);

    // Clear the list of exceptions
    if(!jsrGetGlobalCached(vm, "event_horizon.uv.event_loop", G_CLEAR_EXCEPTIONS,
                           sym_g_clearExceptions)) {
        return false;
    }

    if(!jsrCall(vm, 0)) return false;

    uv_loop_t* loop = EventLoop_getUVLoop(vm, 0);
    if(!loop) return false;

    uv_signal_t sigint;
    int sigintRes = uv_signal_init(loop, &sigint);
    if(sigintRes < 0) {
        StatusException_raise(vm, sigintRes);
        return false;
    }

    sigintRes = uv_signal_start(&sigint, sigintCallback, SIGINT);
    if(sigintRes < 0) {
        closeRunSignalHandle(loop, &sigint);
        StatusException_raise(vm, sigintRes);
        return false;
    }

    uv_unref((uv_handle_t*)&sigint);

    int res = uv_run(loop, mode);
    closeRunSignalHandle(loop, &sigint);

    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    // Check for any exceptions raised during `uv_run_loop` callbacks and throw them
    if(!jsrGetGlobalCached(vm, "event_horizon.uv.event_loop", G_GET_EXCEPTIONS,
                           sym_g_getExceptions)) {
        return false;
    }

    if(!jsrCall(vm, 0)) return false;
    if(jsrListGetLength(vm, -1) != 0) {
        LoopExecutionException_raise(vm, -1);
        return false;
    }
    jsrPop(vm);

    jsrPushNull(vm);
    return true;
}

bool EventLoop_stop(JStarVM* vm) {
    uv_loop_t* loop = EventLoop_getUVLoop(vm, 0);
    if(!loop) return false;
    uv_stop(loop);
    jsrPushNull(vm);
    return true;
}

bool EventLoop_close(JStarVM* vm) {
    uv_loop_t* loop = getUVLoopUserdata(vm, 0);
    if(!loop) return false;

    LoopMetadata* metadata = loop->data;
    if(metadata->closed) {
        jsrPushNull(vm);
        return true;
    }

    int res = uv_loop_close(loop);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    metadata->closed = true;
    if(!setLoopClosed(vm, 0, true)) return false;

    jsrPushNull(vm);
    return true;
}

bool EventLoop_alive(JStarVM* vm) {
    uv_loop_t* loop = EventLoop_getUVLoop(vm, 0);
    if(!loop) return false;
    jsrPushBoolean(vm, uv_loop_alive(loop) != 0);
    return true;
}

bool EventLoop_walk(JStarVM* vm) {
    JSR_CHECK(Function, 1, "callback");

    uv_loop_t* loop = EventLoop_getUVLoop(vm, 0);
    if(!loop) return false;

    jsrPushValue(vm, 1);
    if(!jsrSetField(vm, 0, M_LOOP_WALK_CALLBACK)) jsrPop(vm);

    uv_walk(loop, &walkCallback, loop);

    // uv_walk is synchronous; release the callback reference immediately so it
    // can be collected rather than staying live until the next walk call.
    jsrPushNull(vm);
    if(!jsrSetField(vm, 0, M_LOOP_WALK_CALLBACK)) jsrPop(vm);

    jsrPushNull(vm);
    return true;
}

static void closeLibuvLoop(void* data) {
    uv_loop_t* loop = data;
    LoopMetadata* metadata = loop->data;
    if(!metadata->closed) {
        // TODO: this could print a warning when a user exits from an async
        // program with `sys.exit`. This happens because, even if there are no
        // open handles, at least one `Prepare` handle (for handling `callSoon`
        // loop calls) is probably open at that point

        // If handles are still open their uv_handle_t memory may already have
        // been freed by the GC (handle userdatas are collected before the loop
        // userdata because they are allocated later). uv_loop_close walks the
        // handle queue and dereferences each handle's flags field, so calling
        // it here would be a use-after-free. Skip the close and accept the
        // resource leak instead.
        if(metadata->openHandles > 0) {
            fprintf(stderr, "warning: event loop GC'd with %d open handle(s).\n",
                    metadata->openHandles);
        } else {
            uv_loop_close(loop);
        }
    }
    free(metadata);
}

bool EventLoop_init(JStarVM* vm) {
    if(!sym_loop) {
        sym_loop = jsrNewSymbol(vm);
        sym_id = jsrNewSymbol(vm);
        sym_handles = jsrNewSymbol(vm);
        sym_ref = jsrNewSymbol(vm);
        sym_get = jsrNewSymbol(vm);
        sym_unref = jsrNewSymbol(vm);

        sym_g_addException = jsrNewSymbol(vm);
        sym_g_clearExceptions = jsrNewSymbol(vm);
        sym_g_getExceptions = jsrNewSymbol(vm);
        sym_g_raiseProgramInterrupt = jsrNewSymbol(vm);
    }

    // Instantiate the libuv loop
    int loopId = EventLoop_getId(vm, 0);
    if(loopId == -1) return false;

    jsrPushUserdata(vm, sizeof(uv_loop_t), &closeLibuvLoop);
    uv_loop_t* loop = jsrGetUserdata(vm, -1);
    uv_loop_init(loop);

    LoopMetadata* metadata = malloc(sizeof(*metadata));
    JSR_ASSERT(metadata, "Out of memory");
    metadata->vm = vm;
    metadata->loopId = loopId;
    metadata->closed = false;
    metadata->openHandles = 0;
    uv_loop_set_data(loop, metadata);

    // this._loop = loop
    jsrSetField(vm, 0, M_LOOP_LOOP);
    jsrPop(vm);

    jsrPushNull(vm);
    return true;
}

uv_loop_t* EventLoop_getUVLoop(JStarVM* vm, int eventLoopSlot) {
    uv_loop_t* loop = getUVLoopUserdata(vm, eventLoopSlot);
    if(!loop) return NULL;

    LoopMetadata* metadata = loop->data;
    if(metadata->closed) {
        EventHorizonException_raise(vm, "Event loop is closed");
        return NULL;
    }

    return loop;
}

int EventLoop_getId(JStarVM* vm, int eventLoopSlot) {
    if(!jsrGetFieldCached(vm, eventLoopSlot, M_LOOP_ID, sym_id)) return -1;
    if(!jsrCheckNumber(vm, -1, "EventLoop." M_LOOP_ID)) return -1;
    int loopId = jsrGetNumber(vm, -1);
    jsrPop(vm);
    return loopId;
}

int EventLoop_registerHandle(JStarVM* vm, int handleSlot, int eventLoopSlot) {
    jsrPushValue(vm, eventLoopSlot);
    if(!jsrGetFieldCached(vm, -1, M_LOOP_HANDLES, sym_handles)) return -1;

    jsrPushValue(vm, handleSlot);
    if(!jsrCallMethodCached(vm, "ref", 1, sym_ref)) return -1;

    JSR_CHECK(Int, -1, "loop._handles.ref()");
    int handleId = jsrGetNumber(vm, -1);
    jsrPop(vm);

    return handleId;
}

bool EventLoop_getHandle(JStarVM* vm, int handleId, int eventLoopSlot) {
    if(!jsrGetFieldCached(vm, eventLoopSlot, M_LOOP_HANDLES, sym_handles)) return false;
    jsrPushNumber(vm, handleId);
    if(!jsrCallMethodCached(vm, "get", 1, sym_get)) return false;
    return true;
}

bool EventLoop_unregisterHandle(JStarVM* vm, int handleId, int eventLoopSlot) {
    jsrPushValue(vm, eventLoopSlot);
    if(!jsrGetFieldCached(vm, -1, M_LOOP_HANDLES, sym_handles)) return false;
    jsrPushNumber(vm, handleId);
    if(!jsrCallMethodCached(vm, "unref", 1, sym_unref)) return false;
    jsrPop(vm);
    return true;
}

void EventLoop_addException(JStarVM* vm, int exceptionSlot) {
    // Normalise a negative (top-relative) slot to an absolute index before
    // any pushes shift the stack top.
    if(exceptionSlot < 0) exceptionSlot = jsrTop(vm) + exceptionSlot + 1;
    jsrGetGlobalCached(vm, "event_horizon.uv.event_loop", G_ADD_EXCEPTION, sym_g_addException);
    jsrPushValue(vm, exceptionSlot);
    jsrCall(vm, 1);
    jsrPop(vm);
}
// end

bool getEventLoopFromId(JStarVM* vm, int loopId) {
    static JStarSymbol* sym_getEventLoop;
    if(!sym_getEventLoop) sym_getEventLoop = jsrNewSymbol(vm);
    if(!jsrGetGlobalCached(vm, "event_horizon.uv.event_loop", "getEventLoop", sym_getEventLoop)) {
        return false;
    }
    jsrPushNumber(vm, loopId);
    if(!jsrCall(vm, 1)) return false;
    return true;
}
