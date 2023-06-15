#include "event_loop.h"

#include <string.h>

#include "callbacks.h"
#include "errors.h"

bool getEventLoopFromId(JStarVM* vm, int loopId) {
    if(!jsrGetGlobal(vm, "event_horizon.event_loop", "getEventLoop")) return false;
    jsrPushNumber(vm, loopId);
    if(jsrCall(vm, 1) != JSR_SUCCESS) return false;
    return true;
}

// class EventLoop
#define G_ADD_EXCEPTION    "_addException"
#define G_CLEAR_EXCEPTIONS "_clearExceptions"
#define G_GET_EXCEPTIONS   "_getExceptions"

bool EventLoop_run(JStarVM* vm) {
    JSR_CHECK(Int, 1, "mode");
    int mode = jsrGetNumber(vm, 1);

    // Clear the list of exceptions
    if(!jsrGetGlobal(vm, "event_horizon.event_loop", G_CLEAR_EXCEPTIONS)) return false;
    if(jsrCall(vm, 0) != JSR_SUCCESS) return false;

    uv_loop_t* loop = EventLoop_getUVLoop(vm, 0);
    if(!loop) return false;

    int res = uv_run(loop, mode);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    // Check for any exceptions raised during `uv_run_loop` callbacks and throw them
    if(!jsrGetGlobal(vm, "event_horizon.event_loop", G_GET_EXCEPTIONS)) return false;
    if(jsrCall(vm, 0) != JSR_SUCCESS) return false;
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

    jsrPushNull(vm);
    return true;
}

static void closeLibuvLoop(void* data) {
    uv_loop_t* loop = data;
    LoopMetadata* metadata = loop->data;
    free(metadata);
    int res = uv_loop_close(loop);
    if(res == UV_EBUSY) {
        fprintf(stderr, "Pending handles during call to `uv_loop_cose`.\n");
    }
}

bool EventLoop_init(JStarVM* vm) {
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
    if(!jsrGetField(vm, eventLoopSlot, M_LOOP_LOOP)) return NULL;
    if(!jsrCheckUserdata(vm, -1, "EventLoop." M_LOOP_LOOP)) return NULL;
    uv_loop_t* loop = jsrGetUserdata(vm, -1);
    jsrPop(vm);
    return loop;
}

int EventLoop_getId(JStarVM* vm, int eventLoopSlot) {
    if(!jsrGetField(vm, eventLoopSlot, M_LOOP_ID)) return -1;
    if(!jsrCheckNumber(vm, -1, "EventLoop." M_LOOP_ID)) return -1;
    int loopId = jsrGetNumber(vm, -1);
    jsrPop(vm);
    return loopId;
}

int EventLoop_registerHandle(JStarVM* vm, int handleSlot, int eventLoopSlot) {
    jsrPushValue(vm, eventLoopSlot);
    if(!jsrGetField(vm, -1, M_LOOP_HANDLES)) return -1;
    jsrPushValue(vm, handleSlot);
    if(jsrCallMethod(vm, "ref", 1) != JSR_SUCCESS) return -1;

    JSR_CHECK(Int, -1, "loop._handles.ref()");
    int handleId = jsrGetNumber(vm, -1);
    jsrPop(vm);

    return handleId;
}

bool EventLoop_getHandle(JStarVM* vm, int handleId, int eventLoopSlot) {
    if(!jsrGetField(vm, eventLoopSlot, M_LOOP_HANDLES)) return false;
    jsrPushNumber(vm, handleId);
    if(jsrCallMethod(vm, "get", 1) != JSR_SUCCESS) return false;
    return true;
}

bool EventLoop_unregisterHandle(JStarVM* vm, int handleId, int eventLoopSlot) {
    jsrPushValue(vm, eventLoopSlot);
    if(!jsrGetField(vm, -1, M_LOOP_HANDLES)) return false;
    jsrPushNumber(vm, handleId);
    if(jsrCallMethod(vm, "unref", 1) != JSR_SUCCESS) return false;
    jsrPop(vm);
    return true;
}

void EventLoop_addException(JStarVM* vm, int exceptionSlot) {
    // If the exception slot is relative, we need to correct it to account for next pushes
    if(exceptionSlot < 0) {
        exceptionSlot -= 1;
    }

    jsrGetGlobal(vm, "event_horizon.event_loop", G_ADD_EXCEPTION);
    jsrPushValue(vm, exceptionSlot);
    jsrCall(vm, 1);

    jsrPop(vm);
}
// end
