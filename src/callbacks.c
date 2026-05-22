#include "callbacks.h"

#include <jstar/jstar.h>

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
    LoopMetadata* metadata = handle->loop->data;
    JStarVM* vm = metadata->vm;

    JStarBuffer alloc;
    jsrBufferInitCapacity(vm, &alloc, suggestedSize);

    buf->base = alloc.data;
    buf->len = alloc.capacity;
}

static void sockReadCallback(uv_handle_t* handle, ssize_t nread, const uv_buf_t* buf,
                             bool pushSockAddr, const struct sockaddr* sa, CallbackType cbType) {
    LoopMetadata* loopMetadata = handle->loop->data;
    HandleMetadata* handleMetadata = handle->data;
    JStarVM* vm = loopMetadata->vm;

    JStarBuffer data = (JStarBuffer){
        .vm = vm,
        .size = nread < 0 ? 0 : nread,
        .capacity = buf->len,
        .data = buf->base,
    };

    if(!tryGetEventLoopAndHandle(vm, handleMetadata->handleId, loopMetadata->loopId)) {
        jsrBufferFree(&data);
        return;
    }

    int handleSlot = jsrTop(vm);
    int slot = 2;  // [loop, handle]

    if(!Handle_getCallback(vm, handleMetadata->callbacks[cbType], false, handleSlot)) {
        jsrBufferFree(&data);
        EventLoop_addException(vm, -1);
        jsrPopN(vm, slot + 1);  // [loop, handle, exception]
        return;
    }
    slot++;  // [loop, handle, callback]

    if(nread >= 0) {
        jsrBufferPush(&data);
    } else {
        jsrBufferFree(&data);
        jsrPushNull(vm);
    }
    slot++;  // [loop, handle, callback, data]

    if(pushSockAddr) {
        if(sa != NULL) {
            pushAddr(vm, sa);
            pushPort(vm, sa);
        } else {
            jsrPushNull(vm);
            jsrPushNull(vm);
        }
        slot += 2;
    }

    if(nread > 0 || nread == UV_EOF) {
        jsrPushNumber(vm, 0);
    } else {
        jsrPushNumber(vm, nread);
    }
    slot++;  // status

    if(!jsrCall(vm, slot - 3)) {
        EventLoop_addException(vm, -1);
    }
    slot = 3;  // [loop, handle, result_or_exception]

    jsrPopN(vm, slot);
}

void readCallback(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    sockReadCallback((uv_handle_t*)stream, nread, buf, false, NULL, READ_CB);
}

void recvCallback(uv_udp_t* udp, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* sa,
                  unsigned int flags) {
    (void)flags;
    sockReadCallback((uv_handle_t*)udp, nread, buf, true, sa, RECV_CB);
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
