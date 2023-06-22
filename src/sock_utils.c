#include "sock_utils.h"

#include "errors.h"

int initSockaddr(const char* address, int port, sockaddr_union* addr) {
    int res = uv_ip4_addr(address, port, &addr->in4);
    if(res) {
        int res = uv_ip6_addr(address, port, &addr->in6);
        if(res) {
            return res;
        }
    }
    return res;
}

bool pushPort(JStarVM* vm, const struct sockaddr* address) {
    const sockaddr_union* un = (sockaddr_union*)address;
    if(un->sa.sa_family == AF_INET) {
        jsrPushNumber(vm, ntohs(un->in4.sin_port));
        return true;
    } else if(address->sa_family == AF_INET6) {
        jsrPushNumber(vm, ntohs(un->in6.sin6_port));
        return true;
    } else {
        JSR_RAISE(vm, "TypeException", "Invalid protocol family: %d", address->sa_family);
    }
}

bool pushAddr(JStarVM* vm, const struct sockaddr* address) {
    const sockaddr_union* un = (sockaddr_union*)address;
    if(un->sa.sa_family == AF_INET) {
        char str[INET_ADDRSTRLEN];
        int res = uv_inet_ntop(AF_INET, &un->in4.sin_addr, str, INET_ADDRSTRLEN);
        if(res < 0) {
            StatusException_raise(vm, res);
            return false;
        }
        jsrPushString(vm, str);
        return true;
    } else if(address->sa_family == AF_INET6) {
        char str[INET6_ADDRSTRLEN];
        int res = uv_inet_ntop(AF_INET6, &un->in6.sin6_addr, str, INET6_ADDRSTRLEN);
        if(res < 0) {
            StatusException_raise(vm, res);
            return false;
        }
        jsrPushString(vm, str);
        return true;
    } else {
        JSR_RAISE(vm, "TypeException", "Invalid protocol family: %d", address->sa_family);
    }
}
