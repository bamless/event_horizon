#include "event_loop.h"

#include <jstar/jstar.h>
#include <string.h>

#include "callbacks.h"
#include "errors.h"

static JStarSymbol* sym_loop;
static JStarSymbol* sym_id;
static JStarSymbol* sym_handles;
static JStarSymbol* sym_ref;
static JStarSymbol* sym_get;
static JStarSymbol* sym_unref;

// class EventLoop
#define G_ADD_EXCEPTION    "_addException"
#define G_CLEAR_EXCEPTIONS "_clearExceptions"
#define G_GET_EXCEPTIONS   "_getExceptions"

bool EventLoop_run(JStarVM* vm) {
    JSR_CHECK(Int, 1, "mode");
    int mode = jsrGetNumber(vm, 1);

    // Clear the list of exceptions
    if(!jsrGetGlobal(vm, "event_horizon.uv.event_loop", G_CLEAR_EXCEPTIONS)) return false;
    if(!jsrCall(vm, 0)) return false;

    uv_loop_t* loop = EventLoop_getUVLoop(vm, 0);
    if(!loop) return false;

    int res = uv_run(loop, mode);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    // Check for any exceptions raised during `uv_run_loop` callbacks and throw them
    if(!jsrGetGlobal(vm, "event_horizon.uv.event_loop", G_GET_EXCEPTIONS)) return false;
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
    free(metadata);
    // TODO: should we close this automatically?
    int res = uv_loop_close(loop);
    if(res == UV_EBUSY) {
        fprintf(stderr, "Pending handles during call to `uv_loop_cose`.\n");
    }
}

bool EventLoop_init(JStarVM* vm) {
    if(!sym_loop) {
        sym_loop = jsrNewSymbol(vm);
        sym_id = jsrNewSymbol(vm);
        sym_handles = jsrNewSymbol(vm);
        sym_ref = jsrNewSymbol(vm);
        sym_get = jsrNewSymbol(vm);
        sym_unref = jsrNewSymbol(vm);
    }

    // Instantiate the libuv loop
    int loopId = EventLoop_getId(vm, 0);
    if(loopId == -1) return false;

    jsrPushUserdata(vm, sizeof(uv_loop_t), &closeLibuvLoop);
    uv_loop_t* loop = jsrGetUserdata(vm, -1);
    uv_loop_init(loop);

    LoopMetadata* metadata = malloc(sizeof(*metadata));
    metadata->vm = vm;
    metadata->loopId = loopId;
    uv_loop_set_data(loop, metadata);

    // this._loop = loop
    jsrSetField(vm, 0, M_LOOP_LOOP);
    jsrPop(vm);

    jsrPushNull(vm);
    return true;
}

uv_loop_t* EventLoop_getUVLoop(JStarVM* vm, int eventLoopSlot) {
    if(!jsrGetFieldCached(vm, eventLoopSlot, M_LOOP_LOOP, sym_loop)) return NULL;
    if(!jsrCheckUserdata(vm, -1, "EventLoop." M_LOOP_LOOP)) return NULL;
    uv_loop_t* loop = jsrGetUserdata(vm, -1);
    jsrPop(vm);
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
    jsrGetGlobal(vm, "event_horizon.uv.event_loop", G_ADD_EXCEPTION);
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
