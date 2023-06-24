#include "stream.h"

#include <memory.h>
#include <uv.h>

#include "callbacks.h"
#include "errors.h"
#include "event_horizon.h"
#include "event_loop.h"
#include "handle.h"

typedef struct {
    uv_write_t req;
    uv_buf_t buf;
    char data[];
} WriteRequest;

void writeCallback(uv_write_t* req, int status) {
    int callbackId = getRequestCallback((uv_req_t*)req);
    uv_handle_t* handle = (uv_handle_t*)req->handle;
    free(req);
    reqCallback(handle, callbackId, true, status);
}

bool Stream_write(JStarVM* vm) {
    JSR_CHECK(String, 1, "data");
    if(!jsrIsNull(vm, 2)) {
        JSR_CHECK(Function, 2, "callback");
    }

    if(!Handle_checkClosing(vm, 0)) {
        return false;
    }

    uv_stream_t* stream = (uv_stream_t*)Handle_getHandle(vm, 0);
    if(!stream) return false;

    int callbackId = -1;
    if(!jsrIsNull(vm, 2)) {
        callbackId = Handle_registerCallbackWithId(vm, 2, 0);
        if(callbackId == -1) {
            return false;
        }
    }

    const char* data = jsrGetString(vm, 1);
    size_t dataLen = jsrGetStringSz(vm, 1);

    // TODO: can do better than copy
    // From libuv:
    // 'Note: The memory pointed to by the buffers must remain valid until the callback gets called'
    WriteRequest* req = malloc(sizeof(*req) + dataLen);
    memcpy(req->data, data, dataLen);
    req->buf.base = req->data;
    req->buf.len = dataLen;
    setRequestCallback((uv_req_t*)req, callbackId);

    int res = uv_write(&req->req, stream, &req->buf, 1, &writeCallback);
    if(res < 0) {
        free(req);
        if(!Handle_unregisterCallbackById(vm, callbackId, 0)) {
            return false;
        }
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool Stream_tryWrite(JStarVM* vm) {
    JSR_CHECK(String, 1, "data");

    uv_stream_t* stream = (uv_stream_t*)Handle_getHandle(vm, 0);
    if(!stream) return false;

    uv_buf_t buf = (uv_buf_t){.base = (char*)jsrGetString(vm, 1), .len = jsrGetStringSz(vm, 1)};

    int res = uv_try_write(stream, &buf, 1);
    if(res == UV_EAGAIN) {
        res = 0;
    }
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNumber(vm, res);
    return true;
}

bool Stream_readStart(JStarVM* vm) {
    JSR_CHECK(Function, 1, "callback");

    if(!Handle_checkClosing(vm, 0)) {
        return false;
    }

    uv_stream_t* stream = (uv_stream_t*)Handle_getHandle(vm, 0);
    if(!stream) return false;

    if(!Handle_registerCallback(vm, 1, READ_CB, 0)) {
        return false;
    }

    int res = uv_read_start(stream, allocCallback, readCallback);
    if(res < 0) {
        if(res != UV_EINVAL && !Handle_unregisterCallback(vm, READ_CB, 0)) {
            return false;
        }
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool Stream_readStop(JStarVM* vm) {
    uv_stream_t* stream = (uv_stream_t*)Handle_getHandle(vm, 0);
    if(!stream) return false;

    if(!Handle_unregisterCallback(vm, READ_CB, 0)) {
        return false;
    }

    uv_read_stop(stream);
    jsrPushNull(vm);
    return true;
}

static void shutdownCallback(uv_shutdown_t* req, int status) {
    int callbackId = getRequestCallback((uv_req_t*)req);
    uv_handle_t* handle = (uv_handle_t*)req->handle;
    free(req);
    reqCallback(handle, callbackId, true, status);
}

bool Stream_shutdown(JStarVM* vm) {
    if(!jsrIsNull(vm, 1)) {
        JSR_CHECK(Function, 1, "callback");
    }

    if(!Handle_checkClosing(vm, 0)) {
        return false;
    }

    uv_stream_t* stream = (uv_stream_t*)Handle_getHandle(vm, 0);
    if(!stream) return false;

    uv_shutdown_t* req = malloc(sizeof(*req));

    int callbackId = -1;
    if(!jsrIsNull(vm, 1)) {
        callbackId = Handle_registerCallbackWithId(vm, 1, 0);
        if(callbackId == -1) {
            free(req);
            return false;
        }
    }

    setRequestCallback((uv_req_t*)req, callbackId);

    int res = uv_shutdown(req, stream, &shutdownCallback);
    if(res < 0) {
        free(req);
        if(!Handle_unregisterCallbackById(vm, callbackId, 0)) {
            return false;
        }
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool Stream_isReadable(JStarVM* vm) {
    uv_stream_t* stream = (uv_stream_t*)Handle_getHandle(vm, 0);
    if(!stream) return false;
    jsrPushBoolean(vm, uv_is_readable(stream));
    return true;
}

bool Stream_isWritable(JStarVM* vm) {
    uv_stream_t* stream = (uv_stream_t*)Handle_getHandle(vm, 0);
    if(!stream) return false;
    jsrPushBoolean(vm, uv_is_writable(stream));
    return true;
}

bool Stream_getWriteQueueSize(JStarVM* vm) {
    uv_stream_t* stream = (uv_stream_t*)Handle_getHandle(vm, 0);
    if(!stream) return false;
    jsrPushNumber(vm, uv_stream_get_write_queue_size(stream));
    return true;
}

static void onConnectionCallback(uv_stream_t* stream, int status) {
    HandleMetadata* metadata = stream->data;
    reqCallback((uv_handle_t*)stream, metadata->callbacks[CONNECT_CB], false, status);
}

bool Stream_rawListen(JStarVM* vm) {
    JSR_CHECK(Function, 1, "callback");
    JSR_CHECK(Int, 2, "backlog");

    if(!Handle_checkClosing(vm, 0)) {
        return false;
    }

    uv_stream_t* stream = (uv_stream_t*)Handle_getHandle(vm, 0);
    if(!stream) return false;

    if(!Handle_registerCallback(vm, 1, CONNECT_CB, 0)) {
        return false;
    }

    int res = uv_listen(stream, jsrGetNumber(vm, 2), &onConnectionCallback);
    if(res < 0) {
        if(!Handle_unregisterCallback(vm, CONNECT_CB, 0)) {
            return false;
        }
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool Stream_rawAccept(JStarVM* vm) {
    JSR_CHECK(Userdata, 1, "client");

    uv_stream_t* stream = (uv_stream_t*)Handle_getHandle(vm, 0);
    if(!stream) return false;

    uv_stream_t* client = jsrGetUserdata(vm, 1);

    int res = uv_accept(stream, client);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}
