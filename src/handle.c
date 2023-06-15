#include "handle.h"

#include "callbacks.h"
#include "errors.h"
#include "event_horizon.h"
#include "event_loop.h"

// class Handle
static void freeHandle(void* data) {
    uv_handle_t* handle = data;
    HandleMetadata* metadata = handle->data;
    free(metadata);
}

static uv_handle_t* pushLibUVHandle(JStarVM* vm, uv_loop_t* loop, uv_handle_type type) {
    switch(type) {
    case UV_TCP:
        jsrPushUserdata(vm, sizeof(uv_tcp_t), &freeHandle);
        uv_tcp_init(loop, jsrGetUserdata(vm, -1));
        break;
    default:
        jsrRaise(vm, "InvalidArgException", "unkown handle type: %d", type);
        return NULL;
    }

    HandleMetadata* metadata = malloc(sizeof(*metadata));
    for(int i = 0; i < NUM_CB; i++) {
        metadata->callbacks[i] = -1;
    }

    uv_handle_t* handle = jsrGetUserdata(vm, -1);
    uv_handle_set_data(handle, metadata);

    return handle;
}

bool Handle_init(JStarVM* vm) {
    if(!jsrGetGlobal(vm, "event_horizon.event_loop", "EventLoop")) return false;
    if(!jsrIs(vm, 1, -1)) JSR_RAISE(vm, "TypeException", "loop must be an `EventLoop`");
    jsrPop(vm);

    uv_loop_t* loop = EventLoop_getUVLoop(vm, 1);

    JSR_CHECK(Int, 2, "handleType");
    int type = jsrGetNumber(vm, 2);

    uv_handle_t* handle = pushLibUVHandle(vm, loop, type);
    if(!handle) return false;

    jsrSetField(vm, 0, M_HANDLE_HANDLE);
    jsrPop(vm);

    int handleId = EventLoop_registerHandle(vm, 0, 1);
    if(handleId == -1) return false;

    HandleMetadata* metadata = handle->data;
    metadata->handleId = handleId;

    jsrPushNull(vm);
    return true;
}

bool Handle_close(JStarVM* vm) {
    if(!jsrIsNull(vm, 1)) {
        JSR_CHECK(Function, 1, "callback");
    }

    jsrPushValue(vm, 0);
    if(jsrCallMethod(vm, "isClosing", 0) != JSR_SUCCESS) return false;
    JSR_CHECK(Boolean, -1, "Handle.isClosing()");

    bool isClosing = jsrGetBoolean(vm, -1);
    jsrPop(vm);

    if(isClosing) {
        EventHorizonException_raise(vm, "Handle is already closed");
        return false;
    }

    uv_handle_t* handle = Handle_getHandle(vm, 0);
    if(!handle) return false;

    if(!jsrIsNull(vm, 1)) {
        int callbackId = Handle_registerCallback(vm, 1, 0);
        if(callbackId == -1) return false;
        HandleMetadata* metadata = handle->data;
        metadata->callbacks[CLOSE_CB] = callbackId;
    }

    uv_close(handle, &closeCallback);

    jsrPushNull(vm);
    return true;
}

bool Handle_isActive(JStarVM* vm) {
    uv_handle_t* handle = Handle_getHandle(vm, 0);
    if(!handle) return false;
    jsrPushBoolean(vm, uv_is_active(handle) != 0);
    return true;
}

bool Handle_isClosing(JStarVM* vm) {
    uv_handle_t* handle = Handle_getHandle(vm, 0);
    if(!handle) return false;
    jsrPushBoolean(vm, uv_is_closing(handle) != 0);
    return true;
}

bool Handle_getEventLoop(JStarVM* vm, int handleSlot) {
    if(!jsrGetField(vm, handleSlot, M_HANDLE_LOOP)) return false;
    return true;
}

uv_handle_t* Handle_getHandle(JStarVM* vm, int handleSlot) {
    if(!jsrGetField(vm, handleSlot, M_HANDLE_HANDLE)) return NULL;
    if(!jsrCheckUserdata(vm, -1, "Handle." M_HANDLE_HANDLE)) return NULL;
    uv_handle_t* handle = jsrGetUserdata(vm, -1);
    jsrPop(vm);
    return handle;
}

int Handle_registerCallback(JStarVM* vm, int callbackSlot, int handleSlot) {
    if(!jsrGetField(vm, handleSlot, M_HANDLE_CALLBACKS)) return -1;
    jsrPushValue(vm, callbackSlot);
    if(jsrCallMethod(vm, "ref", 1) != JSR_SUCCESS) return -1;

    if(!jsrCheckInt(vm, -1, "Handle._callbacks.ref()")) {
        jsrPop(vm);
        return -1;
    }

    int callbackId = jsrGetNumber(vm, -1);
    jsrPop(vm);

    return callbackId;
}

bool Handle_getCallback(JStarVM* vm, int callbackId, bool unregister, int handleSlot) {
    if(!jsrGetField(vm, handleSlot, M_HANDLE_CALLBACKS)) return false;
    jsrPushNumber(vm, callbackId);
    if(jsrCallMethod(vm, "get", 1) != JSR_SUCCESS) return false;

    if(unregister && !Handle_unregisterCallback(vm, callbackId, handleSlot)) {
        jsrPop(vm);
        return false;
    }

    return true;
}

bool Handle_unregisterCallback(JStarVM* vm, int callbackId, int handleSlot) {
    if(!jsrGetField(vm, handleSlot, M_HANDLE_CALLBACKS)) return false;
    jsrPushNumber(vm, callbackId);
    if(jsrCallMethod(vm, "unref", 1) != JSR_SUCCESS) return false;
    jsrPop(vm);
    return true;
}
// end
