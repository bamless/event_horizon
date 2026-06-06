#include "callbacks.h"

#include <jstar/jstar.h>
#include <string.h>

#include "dns.h"
#include "event_loop.h"
#include "handle.h"
#include "sock_utils.h"

static bool tryGetEventLoop(JStarVM* vm, int loopId) {
    if(!getEventLoopFromId(vm, loopId)) {
        EventLoop_addException(vm, -1);
        jsrPop(vm);
        return false;
    }
    return true;
}

static bool tryGetHandle(JStarVM* vm, int handleId, int loopSlot) {
    if(!EventLoop_getHandle(vm, handleId, loopSlot)) {
        EventLoop_addException(vm, -1);
        jsrPop(vm);
        return false;
    }
    return true;
}

static bool tryGetEventLoopAndHandle(JStarVM* vm, int handleId, int loopId) {
    if(tryGetEventLoop(vm, loopId)) {
        if(tryGetHandle(vm, handleId, -1)) {
            return true;
        }
        jsrPop(vm);
    }
    return false;
}

static bool tryUnregisterHandle(JStarVM* vm, int handleId, int loopSlot) {
    if(!EventLoop_unregisterHandle(vm, handleId, loopSlot)) {
        EventLoop_addException(vm, -1);
        jsrPop(vm);
        return false;
    }
    return true;
}

void closeCallback(uv_handle_t* handle) {
    HandleMetadata* handleMetadata = handle->data;
    LoopMetadata* loopMetadata = handle->loop->data;
    JStarVM* vm = loopMetadata->vm;

    loopMetadata->openHandles--;

    if(!tryGetEventLoopAndHandle(vm, handleMetadata->handleId, loopMetadata->loopId)) return;
    int loopSlot = jsrTop(vm) - 1;
    int handleSlot = jsrTop(vm);

    if(!tryUnregisterHandle(vm, handleMetadata->handleId, loopSlot)) {
        jsrPopN(vm, 2);
        return;
    }

    int closeCallback = handleMetadata->callbacks[CLOSE_CB];
    if(closeCallback != -1) {
        if(!Handle_getCallback(vm, closeCallback, true, handleSlot) || !jsrCall(vm, 0)) {
            EventLoop_addException(vm, -1);
        }
        jsrPop(vm);
    }

    jsrPopN(vm, 2);
}

void reqCallback(uv_handle_t* handle, int callbackId, bool unregister, int dataRef, int status) {
    HandleMetadata* handleMetadata = handle->data;
    LoopMetadata* loopMetadata = handle->loop->data;
    JStarVM* vm = loopMetadata->vm;

    if(!tryGetEventLoopAndHandle(vm, handleMetadata->handleId, loopMetadata->loopId)) return;
    int handleSlot = jsrTop(vm);

    if(dataRef != -1) {
        if(!Handle_popPending(vm, dataRef, handleSlot)) {
            EventLoop_addException(vm, -1);
            jsrPop(vm);
        }
    }

    if(callbackId != -1) {
        if(!Handle_getCallback(vm, callbackId, unregister, handleSlot) ||
           (jsrPushNumber(vm, status), !jsrCall(vm, 1))) {
            EventLoop_addException(vm, -1);
            jsrPop(vm);
        }
        jsrPop(vm);
    }

    jsrPopN(vm, 2);
}

void connectCallback(uv_connect_t* req, int status) {
    int callbackId = getRequestCallback((uv_req_t*)req);
    uv_handle_t* handle = (uv_handle_t*)req->handle;
    free(req);
    reqCallback(handle, callbackId, true, -1, status);
}

void allocCallback(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf) {
    // Hand libuv our per-loop staging buffer instead of allocating on every read.
    // The buffer is copied into a fresh J* string in sockReadCallback before the
    // user callback fires, so the slot is always free for the next alloc_cb call.
    (void)suggestedSize;
    LoopMetadata* metadata = handle->loop->data;
    buf->base = metadata->readBuf;
    buf->len = LOOP_READ_BUF_SIZE;
}

static void dispatchRead(uv_handle_t* handle, const unsigned char* data, int nread,
                         CallbackType cbType, bool wantAddr, const struct sockaddr* sa,
                         bool unregister) {
    HandleMetadata* handleMetadata = handle->data;
    LoopMetadata* loopMetadata = handle->loop->data;
    JStarVM* vm = loopMetadata->vm;

    if(!tryGetEventLoopAndHandle(vm, handleMetadata->handleId, loopMetadata->loopId)) return;

    int handleSlot = jsrTop(vm);
    int slot = 2;  // [loop, handle]
    int callbackId = handleMetadata->callbacks[cbType];
    if(callbackId == -1) {
        jsrPopN(vm, slot);
        return;
    }

    if(unregister) handleMetadata->callbacks[cbType] = -1;
    if(!Handle_getCallback(vm, callbackId, unregister, handleSlot)) {
        EventLoop_addException(vm, -1);
        jsrPopN(vm, slot + 1);  // [loop, handle, exception]
        return;
    }
    slot++;  // [loop, handle, callback]

    if(nread > 0) {
        JStarBuffer buf;
        jsrBufferInitCapacity(vm, &buf, nread);
        memcpy(buf.data, data, (size_t)nread);
        buf.size = (size_t)nread;
        jsrBufferPush(&buf);
    } else {
        jsrPushNull(vm);
    }
    slot++;  // [loop, handle, callback, data_or_null]

    if(wantAddr) {
        if(sa) {
            pushAddr(vm, sa);
            pushPort(vm, sa);
        } else {
            jsrPushNull(vm);
            jsrPushNull(vm);
        }
        slot += 2;  // [loop, handle, callback, data_or_null, addr, port]
    }

    // Normalize status: success (nread > 0) and UV_EOF map to 0. Negative codes other than UV_EOF
    // carry the raw error through.
    jsrPushNumber(vm, (nread >= 0 || nread == UV_EOF) ? 0 : nread);
    slot++;  // status

    if(!jsrCall(vm, slot - 3)) {
        EventLoop_addException(vm, -1);
    }
    slot = 3;  // [loop, handle, result_or_exception]

    jsrPopN(vm, slot);
}

void deliverRead(uv_handle_t* handle, const unsigned char* data, int nread) {
    dispatchRead(handle, data, nread, READ_CB, false, NULL, false);
}

void cancelRead(uv_handle_t* handle, int status) {
    dispatchRead(handle, NULL, status, READ_CB, false, NULL, true);
}

void readCallback(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    deliverRead((uv_handle_t*)stream, (const unsigned char*)buf->base, (int)nread);
}

void recvCallback(uv_udp_t* udp, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* sa,
                  unsigned int flags) {
    (void)flags;
    dispatchRead((uv_handle_t*)udp, (const unsigned char*)buf->base, (int)nread, RECV_CB, true, sa,
                 false);
}

static void voidHandleCallback(uv_handle_t* handle, CallbackType cbType) {
    HandleMetadata* handleMetadata = handle->data;
    LoopMetadata* loopMetadata = handle->loop->data;
    JStarVM* vm = loopMetadata->vm;

    if(!tryGetEventLoopAndHandle(vm, handleMetadata->handleId, loopMetadata->loopId)) return;
    int handleSlot = jsrTop(vm);

    int callback = handleMetadata->callbacks[cbType];
    if(callback != -1) {
        if(!Handle_getCallback(vm, callback, false, handleSlot) || !jsrCall(vm, 0)) {
            EventLoop_addException(vm, -1);
        }
        jsrPop(vm);
    }

    jsrPopN(vm, 2);
}

void idleCallback(uv_idle_t* idle) {
    voidHandleCallback((uv_handle_t*)idle, IDLE_CB);
}

void prepareCallback(uv_prepare_t* prepare) {
    voidHandleCallback((uv_handle_t*)prepare, PREPARE_CB);
}

void timerCallback(uv_timer_t* timer) {
    voidHandleCallback((uv_handle_t*)timer, TIMER_CB);
}

void walkCallback(uv_handle_t* handle, void* arg) {
    uv_loop_t* loop = arg;
    HandleMetadata* handleMetadata = handle->data;
    LoopMetadata* loopMetadata = loop->data;
    JStarVM* vm = loopMetadata->vm;

    static JStarSymbol* sym_walk_callback;
    if(!sym_walk_callback) sym_walk_callback = jsrNewSymbol(vm);

    if(!tryGetEventLoopAndHandle(vm, handleMetadata->handleId, loopMetadata->loopId)) return;
    int loopSlot = jsrTop(vm) - 1;
    int handleSlot = jsrTop(vm);
    int slot = 2;  // [loop, handle]

    if(!jsrGetFieldCached(vm, loopSlot, M_LOOP_WALK_CALLBACK, sym_walk_callback)) {
        jsrPopN(vm, slot + 1);  // [loop, handle, exception]
        return;
    }
    slot++;  // [loop, handle, walkCallback]

    jsrPushValue(vm, handleSlot);
    slot++;  // [loop, handle, walkCallback, handle_copy]

    // jsrCall pops [walkCallback, handle_copy] and pushes result (or exception on
    // failure); net change is always -1 regardless of success or failure.
    if(!jsrCall(vm, 1)) {
        jsrPrintStacktrace(vm, -1);
    }
    slot--;  // [loop, handle, result_or_exception]

    jsrPop(vm);
    slot--;  // [loop, handle]

    jsrPopN(vm, slot);
}

void getAddrInfoCallback(uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
    LoopMetadata* metadata = req->loop->data;
    JStarVM* vm = metadata->vm;
    int callbackId = getRequestCallback((uv_req_t*)req);
    free(req);

    if(!tryGetEventLoop(vm, metadata->loopId)) {
        freeaddrinfo(res);
        return;
    }
    int slot = 1;  // [loop]

    if(!dns_getCallback(vm, true, callbackId)) {
        freeaddrinfo(res);
        EventLoop_addException(vm, -1);
        jsrPopN(vm, slot + 1);  // [loop, exception]
        return;
    }
    slot++;  // [loop, callback]

    jsrPushList(vm);
    slot++;  // [loop, callback, list]

    struct addrinfo* info = res;
    while(info) {
        if(!pushAddr(vm, info->ai_addr)) {
            freeaddrinfo(res);
            EventLoop_addException(vm, -1);
            jsrPopN(vm, slot + 1);  // [loop, callback, list] + exception from pushAddr
            return;
        }
        slot++;  // [loop, callback, list, addr]

        if(!pushPort(vm, info->ai_addr)) {
            freeaddrinfo(res);
            EventLoop_addException(vm, -1);
            jsrPopN(vm, slot + 1);  // [loop, callback, list, addr] + exception from pushPort
            return;
        }
        slot++;  // [loop, callback, list, addr, port]

        jsrPushTuple(vm, 2);
        slot--;  // pops addr+port, pushes tuple; net -1

        jsrListAppend(vm, -2);

        jsrPop(vm);
        slot--;  // pops tuple

        info = info->ai_next;
    }
    freeaddrinfo(res);

    jsrPushNumber(vm, status);
    slot++;  // [loop, callback, list, status]

    if(!jsrCall(vm, 2)) {
        EventLoop_addException(vm, -1);
    }
    slot -= 2;  // [loop, result]

    jsrPopN(vm, slot);
}

extern inline void setRequestCallback(uv_req_t* req, int callbackId);
extern inline int getRequestCallback(uv_req_t* req);
