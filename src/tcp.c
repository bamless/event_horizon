#include "tcp.h"

#include <uv.h>

#include "callbacks.h"
#include "errors.h"
#include "event_horizon.h"
#include "handle.h"
#include "sock_utils.h"

// class TCP
bool TCP_connect(JStarVM* vm) {
    JSR_CHECK(String, 1, "addr");
    JSR_CHECK(Int, 2, "port");
    if(!jsrIsNull(vm, 3)) {
        JSR_CHECK(Function, 3, "callback");
    }

    if(!Handle_checkClosing(vm, 0)) {
        return false;
    }

    sockaddr_union sa;
    int res = initSockaddr(jsrGetString(vm, 1), jsrGetNumber(vm, 2), &sa);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    uv_tcp_t* tcp = (uv_tcp_t*)Handle_getHandle(vm, 0);
    if(!tcp) return false;

    uv_connect_t* req = malloc(sizeof(*req));

    int callbackId = -1;
    if(!jsrIsNull(vm, 3)) {
        callbackId = Handle_registerCallbackWithId(vm, 3, 0);
        if(callbackId == -1) {
            free(req);
            return false;
        }
    }

    setRequestCallback((uv_req_t*)req, callbackId);

    res = uv_tcp_connect(req, tcp, &sa.sa, &connectCallback);
    if(res < 0) {
        free(req);
        if(!Handle_unregisterCallbackById(vm, callbackId, 0)) return false;
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool TCP_bind(JStarVM* vm) {
    JSR_CHECK(String, 1, "addr");
    JSR_CHECK(Int, 2, "port");
    JSR_CHECK(Boolean, 3, "ipv6Only");

    uv_tcp_t* tcp = (uv_tcp_t*)Handle_getHandle(vm, 0);
    if(!tcp) return false;

    sockaddr_union sa;
    int res = initSockaddr(jsrGetString(vm, 1), jsrGetNumber(vm, 2), &sa);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    unsigned int flags = 0;
    if(jsrGetBoolean(vm, 3)) {
        flags |= UV_TCP_IPV6ONLY;
    }

    res = uv_tcp_bind(tcp, &sa.sa, flags);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool TCP_sockName(JStarVM* vm) {
    uv_tcp_t* tcp = (uv_tcp_t*)Handle_getHandle(vm, 0);
    if(!tcp) return false;

    sockaddr_union un;
    int len = sizeof(un);

    int res = uv_tcp_getsockname(tcp, &un.sa, &len);
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

bool TCP_peerName(JStarVM* vm) {
    uv_tcp_t* tcp = (uv_tcp_t*)Handle_getHandle(vm, 0);
    if(!tcp) return false;

    sockaddr_union un;
    int len = sizeof(un);

    int res = uv_tcp_getpeername(tcp, &un.sa, &len);
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
// end
