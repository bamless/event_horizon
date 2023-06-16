#include "udp.h"

#include "errors.h"
#include "handle.h"
#include "sock_utils.h"
#include "uv.h"

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
    if(!jsrIsNull(vm, 1) && !jsrIsString(vm, 1)) {
        JSR_RAISE(vm, "TypeException", "addr must be either null or a String");
    }
    if(!jsrIsNull(vm, 1)) {
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
// end
