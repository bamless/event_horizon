#ifndef EVENT_HORIZON_H
#define EVENT_HORIZON_H

#include <jstar/jstar.h>
#include <uv.h>

inline void setRequestCallback(uv_req_t* req, int callbackId) {
    req->data = (void*)(uintptr_t)callbackId;
}

inline int getRequestCallback(uv_req_t* req) {
    return (int)(uintptr_t)req->data;
}

#endif
