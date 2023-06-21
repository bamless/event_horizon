#include "udp.h"

#include <string.h>
#include <uv.h>

#include "callbacks.h"
#include "errors.h"
#include "event_horizon.h"
#include "handle.h"
#include "sock_utils.h"

// class UDP
bool UDP_bind(JStarVM* vm) {
    JSR_CHECK(String, 1, "addr");
    JSR_CHECK(Int, 2, "port");

    uv_udp_t* udp = (uv_udp_t*)Handle_getHandle(vm, 0);
    if(!udp) return false;

    sockaddr_union sa;
    int res = initSockaddr(jsrGetString(vm, 1), jsrGetNumber(vm, 2), &sa);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    res = uv_udp_bind(udp, &sa.sa, 0);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool UDP_connect(JStarVM* vm) {
    if(!jsrIsNull(vm, 1)) {
        JSR_CHECK(String, 1, "addr");
    }
    if(!jsrIsNull(vm, 2)) {
        JSR_CHECK(Int, 2, "port");
    }

    uv_udp_t* udp = (uv_udp_t*)Handle_getHandle(vm, 0);
    if(!udp) return false;

    int res;
    if(jsrIsString(vm, 1)) {
        sockaddr_union sa;
        res = initSockaddr(jsrGetString(vm, 1), jsrGetNumber(vm, 2), &sa);

        if(res < 0) {
            StatusException_raise(vm, res);
            return false;
        }

        res = uv_udp_connect(udp, &sa.sa);
    } else {
        res = uv_udp_connect(udp, NULL);
    }

    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

typedef struct {
    uv_udp_send_t req;
    uv_buf_t buf;
    char data[];
} SendRequest;

void sendCallback(uv_udp_send_t* req, int status) {
    int callbackId = getRequestCallback((uv_req_t*)req);
    uv_handle_t* handle = (uv_handle_t*)req->handle;
    free(req);
    statusCallback(handle, callbackId, true, status);
}

bool UDP_send(JStarVM* vm) {
    JSR_CHECK(String, 1, "data");
    if(!jsrIsNull(vm, 2)) {
        JSR_CHECK(String, 2, "addr");
    }
    if(!jsrIsNull(vm, 3)) {
        JSR_CHECK(Int, 3, "port");
    }
    if(!jsrIsNull(vm, 4)) {
        JSR_CHECK(Function, 4, "callback");
    }

    if(!Handle_checkClosing(vm, 0)) {
        return false;
    }

    uv_udp_t* udp = (uv_udp_t*)Handle_getHandle(vm, 0);
    if(!udp) return false;

    struct sockaddr* sa = NULL;

    sockaddr_union sun;
    if(!jsrIsNull(vm, 2)) {
        int res = initSockaddr(jsrGetString(vm, 2), jsrGetNumber(vm, 3), &sun);
        if(res < 0) {
            StatusException_raise(vm, res);
            return false;
        }
        sa = &sun.sa;
    }

    int callbackId = -1;
    if(!jsrIsNull(vm, 4)) {
        callbackId = Handle_registerCallback(vm, 4, 0);
        if(callbackId == -1) {
            return false;
        }
    }

    const char* data = jsrGetString(vm, 1);
    size_t dataLen = jsrGetStringSz(vm, 1);

    // TODO: can do better than copy
    // From libuv:
    // 'Note: The memory pointed to by the buffers must remain valid until the callback gets called'
    SendRequest* req = malloc(sizeof(*req) + dataLen);
    memcpy(req->data, data, dataLen);
    req->buf.base = req->data;
    req->buf.len = dataLen;
    setRequestCallback((uv_req_t*)req, callbackId);

    int res = uv_udp_send(&req->req, udp, &req->buf, 1, sa, &sendCallback);
    if(res < 0) {
        free(req);
        if(callbackId != -1 && !Handle_unregisterCallbackById(vm, callbackId, 0)) {
            return false;
        }
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool UDP_recvStart(JStarVM* vm) {
    JSR_CHECK(Function, 1, "callback");

    if(!Handle_checkClosing(vm, 0)) {
        return false;
    }

    uv_udp_t* udp = (uv_udp_t*)Handle_getHandle(vm, 0);
    if(!udp) return false;

    int callbackId = Handle_registerCallback(vm, 1, 0);
    if(callbackId == -1) {
        return false;
    }
    HandleMetadata* metadata = udp->data;
    metadata->callbacks[RECV_CB] = callbackId;

    int res = uv_udp_recv_start(udp, &allocCallback, &recvCallback);
    if(res < 0) {
        if(!Handle_unregisterCallbackById(vm, callbackId, 0)) {
            return false;
        }
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool UDP_recvStop(JStarVM* vm) {
    uv_udp_t* udp = (uv_udp_t*)Handle_getHandle(vm, 0);
    if(!udp) return false;

    if(!Handle_unregisterCallback(vm, RECV_CB, 0)) {
        return false;
    }

    int res = uv_udp_recv_stop(udp);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}
// end
