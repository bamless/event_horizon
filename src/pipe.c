#include "pipe.h"

#include <assert.h>
#include <jstar/jstar.h>
#include <uv.h>

#include "callbacks.h"
#include "errors.h"
#include "event_loop.h"
#include "handle.h"

bool Pipe_open(JStarVM* vm) {
    JSR_CHECK(Number, 1, "fd");

    uv_pipe_t* pipe = (uv_pipe_t*)Handle_getHandle(vm, 0);
    if(!pipe) return false;

    uv_file fd = (uv_file)jsrGetNumber(vm, 1);
    int res = uv_pipe_open(pipe, fd);

    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool Pipe_bind(JStarVM* vm) {
    JSR_CHECK(String, 1, "name");

    uv_pipe_t* pipe = (uv_pipe_t*)Handle_getHandle(vm, 0);
    if(!pipe) return false;

    int res = uv_pipe_bind(pipe, jsrGetString(vm, 1));
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool Pipe_connect(JStarVM* vm) {
    JSR_CHECK(String, 1, "name");
    JSR_CHECK(Function, 2, "callback");

    if(!Handle_checkClosing(vm, 0)) {
        return false;
    }

    uv_pipe_t* pipe = (uv_pipe_t*)Handle_getHandle(vm, 0);
    if(!pipe) return false;

    uv_connect_t* req = malloc(sizeof(*req));

    int callbackId = -1;
    if(!jsrIsNull(vm, 2)) {
        callbackId = Handle_registerCallbackWithId(vm, 2, 0);
        if(callbackId == -1) {
            free(req);
            return false;
        }
    }

    setRequestCallback((uv_req_t*)req, callbackId);
    uv_pipe_connect(req, pipe, jsrGetString(vm, 1), &connectCallback);

    jsrPushNull(vm);
    return true;
}

bool Pipe_sockName(JStarVM* vm) {
    uv_pipe_t* pipe = (uv_pipe_t*)Handle_getHandle(vm, 0);
    if(!pipe) return false;

    size_t size = 0;
    int res = uv_pipe_getsockname(pipe, NULL, &size);
    assert(res == UV_ENOBUFS);

    JStarBuffer buf;
    jsrBufferInitCapacity(vm, &buf, size);
    buf.size = size;

    res = uv_pipe_getsockname(pipe, buf.data, &size);
    if(res < 0) {
        jsrBufferFree(&buf);
        StatusException_raise(vm, res);
        return false;
    }

    jsrBufferPush(&buf);
    return true;
}

bool Pipe_peerName(JStarVM* vm) {
    uv_pipe_t* pipe = (uv_pipe_t*)Handle_getHandle(vm, 0);
    if(!pipe) return false;

    size_t size = 0;
    int res = uv_pipe_getpeername(pipe, NULL, &size);
    assert(res == UV_ENOBUFS);

    JStarBuffer buf;
    jsrBufferInitCapacity(vm, &buf, size);
    buf.size = size;

    res = uv_pipe_getpeername(pipe, buf.data, &size);
    if(res < 0) {
        jsrBufferFree(&buf);
        StatusException_raise(vm, res);
        return false;
    }

    jsrBufferPush(&buf);
    return true;
}

bool uvPipe(JStarVM* vm) {
    JSR_CHECK(Boolean, 2, "ipc");

    uv_pipe_t* handle = (uv_pipe_t*)pushLibUVHandle(vm, sizeof(uv_pipe_t));
    uv_loop_t* loop = EventLoop_getUVLoop(vm, 1);
    if(!loop) return false;

    uv_pipe_init(loop, handle, jsrGetBoolean(vm, 2));
    return true;
}
