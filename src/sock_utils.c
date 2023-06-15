#include "sock_utils.h"

int initSockaddr(const char* address, int port, sockaddr_union* addr) {
    int res = uv_ip4_addr(address, port, &addr->s4);
    if(res) {
        int res = uv_ip6_addr(address, port, &addr->s6);
        if(res) {
            return res;
        }
    }
    return res;
}
