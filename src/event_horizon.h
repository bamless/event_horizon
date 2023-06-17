#ifndef EVENT_HORIZON_H
#define EVENT_HORIZON_H

#include <jstar/jstar.h>
#include <uv.h>

typedef struct LoopMetadata {
    JStarVM* vm;
    int loopId;
} LoopMetadata;

typedef enum CallbackType {
    CLOSE_CB,
    READ_CB,
    RECV_CB,
    CONNECT_CB,
    NUM_CB,
} CallbackType;

typedef struct HandleMetadata {
    int handleId;
    int callbacks[NUM_CB];
} HandleMetadata;

inline void setRequestCallback(uv_req_t* req, int callbackId) {
    req->data = (void*)(uintptr_t)callbackId;
}

inline int getRequestCallback(uv_req_t* req) {
    return (int)(uintptr_t)req->data;
}

#endif
