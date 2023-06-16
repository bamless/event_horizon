#include "tcp.h"

#include "callbacks.h"
#include "errors.h"
#include "event_horizon.h"
#include "event_loop.h"
#include "handle.h"
#include "sock_utils.h"
#include "uv.h"

// class TCP
static void connectCallback(uv_connect_t* req, int status) {
    int callbackId = getRequestCallback((uv_req_t*)req);
    statusCallback((uv_handle_t*)req->handle, callbackId, true, status);
    free(req);
}

bool TCP_connect(JStarVM* vm) {
    JSR_CHECK(String, 1, "addr");
    JSR_CHECK(Int, 2, "port");
    if(!jsrIsNull(vm, 3)) {
        JSR_CHECK(Function, 3, "callback");
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
        callbackId = Handle_registerCallback(vm, 3, 0);
        if(callbackId == -1) {
            free(req);
            return false;
        }
    }

    setRequestCallback((uv_req_t*)req, callbackId);

    res = uv_tcp_connect(req, tcp, &sa.sa, &connectCallback);
    if(res < 0) {
        free(req);
        if(!Handle_unregisterCallback(vm, callbackId, 0)) return false;
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool TCP_bind(JStarVM* vm) {
    JSR_CHECK(String, 1, "addr");
    JSR_CHECK(Int, 2, "port");

    uv_tcp_t* tcp = (uv_tcp_t*)Handle_getHandle(vm, 0);
    if(!tcp) return false;

    sockaddr_union sa;
    int res = initSockaddr(jsrGetString(vm, 1), jsrGetNumber(vm, 2), &sa);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    res = uv_tcp_bind(tcp, &sa.sa, 0);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

static bool pushAddrAndPort(JStarVM* vm, const struct sockaddr_storage* storage) {
    if(storage->ss_family == AF_INET) {
        char str[INET_ADDRSTRLEN];
        const struct sockaddr_in* sa = (const struct sockaddr_in*)storage;
        int res = uv_inet_ntop(AF_INET, &sa->sin_addr, str, INET_ADDRSTRLEN);
        if(res < 0) {
            StatusException_raise(vm, res);
            return false;
        }

        jsrPushString(vm, str);
        return true;
    } else if(storage->ss_family == AF_INET6) {
        char str[INET6_ADDRSTRLEN];
        const struct sockaddr_in6* sa = (const struct sockaddr_in6*)storage;
        int res = uv_inet_ntop(AF_INET6, &sa->sin6_addr, str, INET6_ADDRSTRLEN);
        if(res < 0) {
            StatusException_raise(vm, res);
            return false;
        }

        jsrPushString(vm, str);
        return true;
    } else {
        JSR_RAISE(vm, "TypeException", "Invalid protocol family: %d", storage->ss_family);
    }
}

bool TCP_sockName(JStarVM* vm) {
    uv_tcp_t* tcp = (uv_tcp_t*)Handle_getHandle(vm, 0);
    if(!tcp) return false;
    
    int len = sizeof(struct sockaddr_storage);
    struct sockaddr_storage storage;
    int res = uv_tcp_getsockname(tcp, (struct sockaddr*)&storage, &len);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    if(!pushAddrAndPort(vm, &storage)) {
        return false;
    }

    return true;
}

bool TCP_peerName(JStarVM* vm) {
    uv_tcp_t* tcp = (uv_tcp_t*)Handle_getHandle(vm, 0);
    if(!tcp) return false;
    
    int len = sizeof(struct sockaddr_storage);
    struct sockaddr_storage storage;
    int res = uv_tcp_getpeername(tcp, (struct sockaddr*)&storage, &len);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    if(!pushAddrAndPort(vm, &storage)) {
        return false;
    }

    return true;
}
// end
