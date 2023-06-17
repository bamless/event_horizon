#ifndef CALLBACKS_H
#define CALLBACKS_H

#include <jstar/jstar.h>
#include <uv.h>

void closeCallback(uv_handle_t* handle);
void statusCallback(uv_handle_t* handle, int callbackId, bool unregister, int status);
void allocCallback(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf);
void readCallback(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
void recvCallback(uv_udp_t* udp, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* sa,
                  unsigned int flags);
void walkCallback(uv_handle_t* handle, void* arg);

#endif
