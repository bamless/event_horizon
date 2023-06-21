#include "sock_utils.h"

#include "errors.h"

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

bool pushPort(JStarVM* vm, const struct sockaddr* address) {
    if(address->sa_family == AF_INET) {
        const struct sockaddr_in* sa = (const struct sockaddr_in*)address;
        jsrPushNumber(vm, ntohs(sa->sin_port));
        return true;
    } else if(address->sa_family == AF_INET6) {
        const struct sockaddr_in6* sa = (const struct sockaddr_in6*)address;
        jsrPushNumber(vm, ntohs(sa->sin6_port));
        return true;
    } else {
        JSR_RAISE(vm, "TypeException", "Invalid protocol family: %d", address->sa_family);
    }
}

bool pushAddr(JStarVM* vm, const struct sockaddr* address) {
    if(address->sa_family == AF_INET) {
        char str[INET_ADDRSTRLEN];
        const struct sockaddr_in* sa = (const struct sockaddr_in*)address;
        int res = uv_inet_ntop(AF_INET, &sa->sin_addr, str, INET_ADDRSTRLEN);
        if(res < 0) {
            StatusException_raise(vm, res);
            return false;
        }
        jsrPushString(vm, str);
        return true;
    } else if(address->sa_family == AF_INET6) {
        char str[INET6_ADDRSTRLEN];
        const struct sockaddr_in6* sa = (const struct sockaddr_in6*)address;
        int res = uv_inet_ntop(AF_INET6, &sa->sin6_addr, str, INET6_ADDRSTRLEN);
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
