#include "stream.h"

#include <memory.h>
#include <uv.h>

#include "callbacks.h"
#include "errors.h"
#include "handle.h"

typedef struct WriteReq {
    uv_write_t req;
    int dataRef;
    struct WriteReq* nextFree;
} WriteReq;

// Global pool of recycled WriteReqs. Safe without locking: the J* VM is
// single-threaded and libuv callbacks fire on the same thread.
static WriteReq* writeReqPool;

static WriteReq* writeReqAcquire(void) {
    if(writeReqPool) {
        WriteReq* req = writeReqPool;
        writeReqPool = req->nextFree;
        return req;
    }
    WriteReq* req = malloc(sizeof(WriteReq));
    JSR_ASSERT(req, "Out of memory");
    return req;
}

static void writeReqRelease(WriteReq* req) {
    req->nextFree = writeReqPool;
    writeReqPool = req;
}

void writeCallback(uv_write_t* req, int status) {
    WriteReq* writeReq = (WriteReq*)req;
    int dataRef = writeReq->dataRef;
    int callbackId = getRequestCallback((uv_req_t*)req);
    uv_handle_t* handle = (uv_handle_t*)req->handle;
    writeReqRelease(writeReq);
    reqCallback(handle, callbackId, true, dataRef, status);
}

bool Stream_rawWrite(JStarVM* vm) {
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
        if(callbackId == -1) return false;
    }

    const char* data = jsrGetString(vm, 1);
    size_t dataSz = jsrGetStringSz(vm, 1);

    int dataRef = Handle_pushPending(vm, 1, 0);
    if(dataRef == -1) return false;

    WriteReq* req = writeReqAcquire();
    req->dataRef = dataRef;
    setRequestCallback((uv_req_t*)req, callbackId);

    uv_buf_t buf = {(char*)data, dataSz};
    int res = uv_write(&req->req, stream, &buf, 1, &writeCallback);
    if(res < 0) {
        writeReqRelease(req);
        if(!Handle_unregisterCallbackById(vm, callbackId, 0)) return false;
        if(!Handle_popPending(vm, dataRef, 0)) return false;
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool Stream_tryWrite(JStarVM* vm) {
    JSR_CHECK(String, 1, "data");

    if(!Handle_checkClosing(vm, 0)) {
        return false;
    }

    uv_stream_t* stream = (uv_stream_t*)Handle_getHandle(vm, 0);
    if(!stream) return false;

    uv_buf_t buf = (uv_buf_t){
        .base = (char*)jsrGetString(vm, 1),
        .len = jsrGetStringSz(vm, 1),
    };

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
        if(res != UV_EINVAL && !Handle_unregisterCallback(vm, READ_CB, 0)) return false;
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
    reqCallback(handle, callbackId, true, -1, status);
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
    JSR_ASSERT(req, "Out of memory");

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
        if(!Handle_unregisterCallbackById(vm, callbackId, 0)) return false;
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
    reqCallback((uv_handle_t*)stream, metadata->callbacks[CONNECT_CB], false, -1, status);
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
        if(!Handle_unregisterCallback(vm, CONNECT_CB, 0)) return false;
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
