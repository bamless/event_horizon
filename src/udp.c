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
// end
