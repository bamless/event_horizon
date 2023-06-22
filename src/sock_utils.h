#ifndef SOCK_UTILS_H
#define SOCK_UTILS_H

#include <jstar/jstar.h>
#include <uv.h>

typedef union sockaddr_union {
    struct sockaddr sa;
    struct sockaddr_in in4;
    struct sockaddr_in6 in6;
    struct sockaddr_storage storage;
} sockaddr_union;

int initSockaddr(const char* address, int port, sockaddr_union* addr);
bool pushPort(JStarVM* vm, const struct sockaddr* address);
bool pushAddr(JStarVM* vm, const struct sockaddr* address);

#endif  // SOCK_UTILS_H
