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
    case UV_UDP:
        jsrPushUserdata(vm, sizeof(uv_udp_t), &freeHandle);
        uv_udp_init(loop, jsrGetUserdata(vm, -1));
        break;
    case UV_IDLE:
        jsrPushUserdata(vm, sizeof(uv_idle_t), &freeHandle);
        uv_idle_init(loop, jsrGetUserdata(vm, -1));
        break;
    case UV_TIMER:
        jsrPushUserdata(vm, sizeof(uv_timer_t), &freeHandle);
        uv_timer_init(loop, jsrGetUserdata(vm, -1));
        break;
    case UV_NAMED_PIPE:
        jsrTupleGet(vm, 0, 3);
        bool ipc = jsrGetBoolean(vm, -1);
        jsrPop(vm);

        jsrPushUserdata(vm, sizeof(uv_pipe_t), &freeHandle);
        uv_pipe_init(loop, jsrGetUserdata(vm, -1), ipc);
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
    if(!jsrGetGlobal(vm, "event_horizon.uv.event_loop", "EventLoop")) return false;
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

    if(!Handle_checkClosing(vm, 0)) {
        return false;
    }

    uv_handle_t* handle = Handle_getHandle(vm, 0);
    if(!handle) return false;

    if(!jsrIsNull(vm, 1) && !Handle_registerCallback(vm, 1, CLOSE_CB, 0)) {
        return false;
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

bool Handle_sendBufferSize(JStarVM* vm) {
    uv_handle_t* handle = Handle_getHandle(vm, 0);
    if(!handle) return false;

    int size = 0;
    int res = uv_send_buffer_size(handle, &size);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNumber(vm, size);
    return true;
}

bool Handle_setSendBufferSize(JStarVM* vm) {
    JSR_CHECK(Int, 1, "size");

    uv_handle_t* handle = Handle_getHandle(vm, 0);
    if(!handle) return false;

    int size = jsrGetNumber(vm, 1);
    if(size <= 0) {
        JSR_RAISE(vm, "InvalidArgException", "size must be > 0");
    }

    int res = uv_send_buffer_size(handle, &size);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNumber(vm, size);
    return true;
}

bool Handle_recvBufferSize(JStarVM* vm) {
    uv_handle_t* handle = Handle_getHandle(vm, 0);
    if(!handle) return false;

    int size = 0;
    int res = uv_recv_buffer_size(handle, &size);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNumber(vm, size);
    return true;
}

bool Handle_setRecvBufferSize(JStarVM* vm) {
    JSR_CHECK(Int, 1, "size");

    uv_handle_t* handle = Handle_getHandle(vm, 0);
    if(!handle) return false;

    int size = jsrGetNumber(vm, 1);
    if(size <= 0) {
        JSR_RAISE(vm, "InvalidArgException", "size must be > 0");
    }

    int res = uv_recv_buffer_size(handle, &size);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNumber(vm, size);
    return true;
}

bool Handle_fileno(JStarVM* vm) {
    uv_handle_t* handle = Handle_getHandle(vm, 0);
    if(!handle) return false;

    uv_os_fd_t fd;
    int res = uv_fileno(handle, &fd);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNumber(vm, fd);
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

bool Handle_registerCallback(JStarVM* vm, int callbackSlot, CallbackType type, int handleSlot) {
    int callbackId = Handle_registerCallbackWithId(vm, callbackSlot, handleSlot);
    if(callbackId == -1) return false;
    uv_handle_t* handle = Handle_getHandle(vm, handleSlot);
    if(!handle) return false;
    HandleMetadata* metadata = handle->data;
    metadata->callbacks[type] = callbackId;
    return true;
}

int Handle_registerCallbackWithId(JStarVM* vm, int callbackSlot, int handleSlot) {
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

    if(unregister && !Handle_unregisterCallbackById(vm, callbackId, handleSlot)) {
        jsrPop(vm);
        return false;
    }

    return true;
}

bool Handle_unregisterCallback(JStarVM* vm, CallbackType type, int handleSlot) {
    uv_handle_t* handle = Handle_getHandle(vm, handleSlot);
    if(!handle) return false;
    HandleMetadata* metadata = handle->data;
    int callbackId = metadata->callbacks[type];
    if(callbackId == -1) return true;
    return Handle_unregisterCallbackById(vm, callbackId, handleSlot);
}

bool Handle_unregisterCallbackById(JStarVM* vm, int callbackId, int handleSlot) {
    if(callbackId == -1) return true;
    if(!jsrGetField(vm, handleSlot, M_HANDLE_CALLBACKS)) return false;
    jsrPushNumber(vm, callbackId);
    if(jsrCallMethod(vm, "unref", 1) != JSR_SUCCESS) return false;
    jsrPop(vm);
    return true;
}

bool Handle_checkClosing(JStarVM* vm, int handleSlot) {
    jsrPushValue(vm, handleSlot);
    if(jsrCallMethod(vm, "isClosing", 0) != JSR_SUCCESS) return false;
    JSR_CHECK(Boolean, -1, "Handle.isClosing()");

    bool isClosing = jsrGetBoolean(vm, -1);
    jsrPop(vm);

    if(isClosing) {
        EventHorizonException_raise(vm, "Handle is already closed");
        return false;
    }

    return true;
}
// end
