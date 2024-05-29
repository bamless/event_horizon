#include "udp.h"

#include <string.h>
#include <uv.h>

#include "callbacks.h"
#include "errors.h"
#include "event_loop.h"
#include "handle.h"
#include "sock_utils.h"

bool UDP_bind(JStarVM* vm) {
    JSR_CHECK(String, 1, "addr");
    JSR_CHECK(Int, 2, "port");
    JSR_CHECK(Int, 3, "flags");

    uv_udp_t* udp = (uv_udp_t*)Handle_getHandle(vm, 0);
    if(!udp) return false;

    sockaddr_union sa;
    int res = initSockaddr(jsrGetString(vm, 1), jsrGetNumber(vm, 2), &sa);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    unsigned int flags = jsrGetNumber(vm, 3);
    res = uv_udp_bind(udp, &sa.sa, flags);
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
    reqCallback(handle, callbackId, true, status);
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
        callbackId = Handle_registerCallbackWithId(vm, 4, 0);
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

bool UDP_trySend(JStarVM* vm) {
    JSR_CHECK(String, 1, "data");
    if(!jsrIsNull(vm, 2)) {
        JSR_CHECK(String, 2, "addr");
    }
    if(!jsrIsNull(vm, 3)) {
        JSR_CHECK(Int, 3, "port");
    }

    uv_udp_t* udp = (uv_udp_t*)Handle_getHandle(vm, 0);
    if(!udp) return false;

    sockaddr_union sa;
    int res = initSockaddr(jsrGetString(vm, 2), jsrGetNumber(vm, 3), &sa);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    uv_buf_t buf = (uv_buf_t){
        .base = (char*)jsrGetString(vm, 1),
        .len = jsrGetStringSz(vm, 1),
    };

    res = uv_udp_try_send(udp, &buf, 1, &sa.sa);
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

bool UDP_recvStart(JStarVM* vm) {
    JSR_CHECK(Function, 1, "callback");

    if(!Handle_checkClosing(vm, 0)) {
        return false;
    }

    uv_udp_t* udp = (uv_udp_t*)Handle_getHandle(vm, 0);
    if(!udp) return false;

    if(!Handle_registerCallback(vm, 1, RECV_CB, 0)) {
        return false;
    }

    int res = uv_udp_recv_start(udp, &allocCallback, &recvCallback);
    if(res < 0) {
        if(!Handle_unregisterCallback(vm, RECV_CB, 0)) {
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

bool UDP_sockName(JStarVM* vm) {
    uv_udp_t* udp = (uv_udp_t*)Handle_getHandle(vm, 0);
    if(!udp) return false;

    sockaddr_union un;
    int len = sizeof(un);

    int res = uv_udp_getsockname(udp, &un.sa, &len);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    if(!pushAddr(vm, &un.sa)) {
        return false;
    }
    if(!pushPort(vm, &un.sa)) {
        return false;
    }

    jsrPushTuple(vm, 2);
    return true;
}

bool UDP_peerName(JStarVM* vm) {
    uv_udp_t* udp = (uv_udp_t*)Handle_getHandle(vm, 0);
    if(!udp) return false;

    sockaddr_union un;
    int len = sizeof(un);

    int res = uv_udp_getpeername(udp, &un.sa, &len);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    if(!pushAddr(vm, &un.sa)) {
        return false;
    }
    if(!pushPort(vm, &un.sa)) {
        return false;
    }

    jsrPushTuple(vm, 2);
    return true;
}

bool UDP_sendQueueSize(JStarVM* vm) {
    uv_udp_t* udp = (uv_udp_t*)Handle_getHandle(vm, 0);
    if(!udp) return false;
    int res = uv_udp_get_send_queue_size(udp);
    jsrPushNumber(vm, res);
    return true;
}

bool UDP_sendQueueCount(JStarVM* vm) {
    uv_udp_t* udp = (uv_udp_t*)Handle_getHandle(vm, 0);
    if(!udp) return false;
    int res = uv_udp_get_send_queue_count(udp);
    jsrPushNumber(vm, res);
    return true;
}

bool UDP_setMembership(JStarVM* vm) {
    JSR_CHECK(String, 1, "multicastAddr");
    JSR_CHECK(String, 2, "interfaceAddr");
    JSR_CHECK(Int, 3, "membership");

    uv_udp_t* udp = (uv_udp_t*)Handle_getHandle(vm, 0);
    if(!udp) return false;
    int res = uv_udp_set_membership(udp, jsrGetString(vm, 1), jsrGetString(vm, 2),
                                    jsrGetNumber(vm, 3));
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool UDP_setSourceMembership(JStarVM* vm) {
    JSR_CHECK(String, 1, "multicastAddr");
    JSR_CHECK(String, 2, "interfaceAddr");
    JSR_CHECK(String, 3, "interfaceAddr");
    JSR_CHECK(Int, 4, "membership");

    uv_udp_t* udp = (uv_udp_t*)Handle_getHandle(vm, 0);
    if(!udp) return false;
    int res = uv_udp_set_source_membership(udp, jsrGetString(vm, 1), jsrGetString(vm, 2),
                                           jsrGetString(vm, 3), jsrGetNumber(vm, 4));
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool UDP_setMulticastLoop(JStarVM* vm) {
    JSR_CHECK(Boolean, 1, "on");

    uv_udp_t* udp = (uv_udp_t*)Handle_getHandle(vm, 0);
    if(!udp) return false;

    int res = uv_udp_set_multicast_loop(udp, jsrGetBoolean(vm, 1));
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool UDP_setMulticastTTL(JStarVM* vm) {
    JSR_CHECK(Int, 1, "ttl");

    uv_udp_t* udp = (uv_udp_t*)Handle_getHandle(vm, 0);
    if(!udp) return false;

    int res = uv_udp_set_multicast_ttl(udp, jsrGetNumber(vm, 1));
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool UDP_setMulticastInterface(JStarVM* vm) {
    JSR_CHECK(String, 1, "interfaceAddr");

    uv_udp_t* udp = (uv_udp_t*)Handle_getHandle(vm, 0);
    if(!udp) return false;

    int res = uv_udp_set_multicast_interface(udp, jsrGetString(vm, 1));
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool UDP_setBroadcast(JStarVM* vm) {
    JSR_CHECK(Boolean, 1, "on");

    uv_udp_t* udp = (uv_udp_t*)Handle_getHandle(vm, 0);
    if(!udp) return false;

    int res = uv_udp_set_broadcast(udp, jsrGetBoolean(vm, 1));
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool uvUDP(JStarVM* vm) {
    uv_udp_t* handle = (uv_udp_t*)pushLibUVHandle(vm, sizeof(uv_udp_t));
    uv_loop_t* loop = EventLoop_getUVLoop(vm, 1);
    if(!loop) return false;
    uv_udp_init(loop, handle);
    return true;
}
