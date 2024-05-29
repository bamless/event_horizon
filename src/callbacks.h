#ifndef CALLBACKS_H
#define CALLBACKS_H

#include <jstar/jstar.h>
#include <uv.h>

typedef enum CallbackType {
    CLOSE_CB,
    READ_CB,
    RECV_CB,
    CONNECT_CB,
    IDLE_CB,
    TIMER_CB,
    NUM_CB,
} CallbackType;

inline void setRequestCallback(uv_req_t* req, int callbackId) {
    req->data = (void*)(uintptr_t)callbackId;
}

inline int getRequestCallback(uv_req_t* req) {
    return (int)(uintptr_t)req->data;
}

void closeCallback(uv_handle_t* handle);
void reqCallback(uv_handle_t* handle, int callbackId, bool unregister, int status);
void connectCallback(uv_connect_t* req, int status);
void allocCallback(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf);
void readCallback(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
void recvCallback(uv_udp_t* udp, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* sa,
                  unsigned int flags);
void idleCallback(uv_idle_t* idle);
void timerCallback(uv_timer_t* timer);
void walkCallback(uv_handle_t* handle, void* arg);
void getAddrInfoCallback(uv_getaddrinfo_t* req, int status, struct addrinfo* res);

#endif
