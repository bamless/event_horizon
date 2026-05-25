#include "handle.h"

#include <jstar/jstar.h>
#include <stdlib.h>

#include "callbacks.h"
#include "errors.h"
#include "event_loop.h"

static JStarSymbol* sym_handle;
static JStarSymbol* sym_callbacks;
static JStarSymbol* sym_pending_data;
static JStarSymbol* sym_ref;
static JStarSymbol* sym_get;
static JStarSymbol* sym_unref;
static JStarSymbol* sym_get_and_unref;

static void freeHandle(void* data) {
    uv_handle_t* handle = data;
    HandleMetadata* metadata = handle->data;
    free(metadata);
}

uv_handle_t* pushLibUVHandle(JStarVM* vm, size_t size) {
    jsrPushUserdata(vm, size, &freeHandle);
    HandleMetadata* metadata = malloc(sizeof(*metadata));
    for(int i = 0; i < NUM_CB; i++) {
        metadata->callbacks[i] = -1;
    }
    uv_handle_t* handle = jsrGetUserdata(vm, -1);
    uv_handle_set_data(handle, metadata);
    return handle;
}

bool Handle_init(JStarVM* vm) {
    if(!sym_handle) {
        sym_handle = jsrNewSymbol(vm);
        sym_callbacks = jsrNewSymbol(vm);
        sym_pending_data = jsrNewSymbol(vm);
        sym_ref = jsrNewSymbol(vm);
        sym_get = jsrNewSymbol(vm);
        sym_unref = jsrNewSymbol(vm);
        sym_get_and_unref = jsrNewSymbol(vm);
    }

    JSR_CHECK(Userdata, 2, "handle");
    uv_handle_t* handle = jsrGetUserdata(vm, 2);

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

bool Handle_handleType(JStarVM* vm) {
    uv_handle_t* handle = Handle_getHandle(vm, 0);
    if(!handle) return false;
    jsrPushNumber(vm, uv_handle_get_type(handle));
    return true;
}

bool Handle_getEventLoop(JStarVM* vm, int handleSlot) {
    if(!jsrGetField(vm, handleSlot, M_HANDLE_LOOP)) return false;
    return true;
}

uv_handle_t* Handle_getHandle(JStarVM* vm, int handleSlot) {
    if(!jsrGetFieldCached(vm, handleSlot, M_HANDLE_HANDLE, sym_handle)) return NULL;
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
    if(!jsrGetFieldCached(vm, handleSlot, M_HANDLE_CALLBACKS, sym_callbacks)) return -1;

    jsrPushValue(vm, callbackSlot);
    if(!jsrCallMethodCached(vm, "ref", 1, sym_ref)) return -1;

    if(!jsrCheckInt(vm, -1, "Handle." M_HANDLE_CALLBACKS ".ref()")) {
        jsrPop(vm);
        return -1;
    }

    int callbackId = jsrGetNumber(vm, -1);
    jsrPop(vm);

    return callbackId;
}

bool Handle_getCallback(JStarVM* vm, int callbackId, bool unregister, int handleSlot) {
    if(!jsrGetFieldCached(vm, handleSlot, M_HANDLE_CALLBACKS, sym_callbacks)) return false;
    jsrPushNumber(vm, callbackId);

    if(unregister) {
        if(!jsrCallMethodCached(vm, "getAndUnref", 1, sym_get_and_unref)) return false;
    } else {
        if(!jsrCallMethodCached(vm, "get", 1, sym_get)) return false;
    }

    return true;
}

bool Handle_unregisterCallback(JStarVM* vm, CallbackType type, int handleSlot) {
    uv_handle_t* handle = Handle_getHandle(vm, handleSlot);
    if(!handle) return false;
    HandleMetadata* metadata = handle->data;
    int callbackId = metadata->callbacks[type];
    if(callbackId == -1) return true;
    // Clear the slot BEFORE unreffing so that any re-entrant code (e.g. the TLS
    // decode loop checking callbacks[READ_CB]) sees -1 immediately, not the now-
    // freed registry index whose slot holds a free-list number rather than a
    // callable — which would cause a TypeException if mistakenly called.
    metadata->callbacks[type] = -1;
    return Handle_unregisterCallbackById(vm, callbackId, handleSlot);
}

bool Handle_unregisterCallbackById(JStarVM* vm, int callbackId, int handleSlot) {
    if(callbackId == -1) return true;
    if(!jsrGetFieldCached(vm, handleSlot, M_HANDLE_CALLBACKS, sym_callbacks)) return false;
    jsrPushNumber(vm, callbackId);
    if(!jsrCallMethodCached(vm, "unref", 1, sym_unref)) return false;
    jsrPop(vm);
    return true;
}

int Handle_pushPending(JStarVM* vm, int dataSlot, int handleSlot) {
    if(!jsrGetFieldCached(vm, handleSlot, M_HANDLE_PENDING_DATA, sym_pending_data)) return -1;

    jsrPushValue(vm, dataSlot);
    if(!jsrCallMethodCached(vm, "ref", 1, sym_ref)) return -1;

    if(!jsrCheckInt(vm, -1, "Handle." M_HANDLE_PENDING_DATA ".ref()")) {
        jsrPop(vm);
        return -1;
    }

    int dataRef = jsrGetNumber(vm, -1);
    jsrPop(vm);

    return dataRef;
}

bool Handle_popPending(JStarVM* vm, int dataRef, int handleSlot) {
    if(dataRef == -1) return true;
    if(!jsrGetFieldCached(vm, handleSlot, M_HANDLE_PENDING_DATA, sym_pending_data)) return false;
    jsrPushNumber(vm, dataRef);
    if(!jsrCallMethodCached(vm, "unref", 1, sym_unref)) return false;
    jsrPop(vm);
    return true;
}

bool Handle_checkClosing(JStarVM* vm, int handleSlot) {
    uv_handle_t* handle = Handle_getHandle(vm, handleSlot);
    if(!handle) return false;

    if(uv_is_closing(handle)) {
        EventHorizonException_raise(vm, "Handle is already closed");
        return false;
    }

    return true;
}
