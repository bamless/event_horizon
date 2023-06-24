#include "callbacks.h"

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
    if(!tryGetEventLoop(vm, loopId)) {
        return false;
    }
    if(!tryGetHandle(vm, handleId, -1)) {
        jsrPop(vm);
        return false;
    }
    return true;
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

    if(!tryGetEventLoopAndHandle(vm, handleMetadata->handleId, loopMetadata->loopId)) {
        return;
    }
    int loopSlot = jsrTop(vm) - 1;
    int handleSlot = jsrTop(vm);

    if(!tryUnregisterHandle(vm, handleMetadata->handleId, loopSlot)) {
        jsrPopN(vm, 2);
        return;
    }

    int closeCallback = handleMetadata->callbacks[CLOSE_CB];
    if(closeCallback != -1) {
        if(!Handle_getCallback(vm, closeCallback, true, handleSlot) ||
           (jsrCall(vm, 0) != JSR_SUCCESS)) {
            EventLoop_addException(vm, -1);
        }
        jsrPop(vm);
    }

    jsrPopN(vm, 2);
}

void reqCallback(uv_handle_t* handle, int callbackId, bool unregister, int status) {
    HandleMetadata* handleMetadata = handle->data;
    LoopMetadata* loopMetadata = handle->loop->data;
    JStarVM* vm = loopMetadata->vm;

    if(!tryGetEventLoopAndHandle(vm, handleMetadata->handleId, loopMetadata->loopId)) {
        return;
    }
    int handleSlot = jsrTop(vm);

    if(callbackId != -1) {
        if(!Handle_getCallback(vm, callbackId, unregister, handleSlot) ||
           (jsrPushNumber(vm, status), jsrCall(vm, 1) != JSR_SUCCESS)) {
            EventLoop_addException(vm, -1);
        }
        jsrPop(vm);
    }

    jsrPopN(vm, 2);
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
        return;
    }
    int handleSlot = jsrTop(vm);

    if(!Handle_getCallback(vm, handleMetadata->callbacks[cbType], false, handleSlot)) {
        jsrBufferFree(&data);
        EventLoop_addException(vm, -1);
        jsrPopN(vm, 3);
        return;
    }

    if(nread >= 0) {
        jsrBufferPush(&data);
    } else {
        jsrBufferFree(&data);
        jsrPushNull(vm);
    }

    if(pushSockAddr) {
        if(sa != NULL) {
            pushAddr(vm, sa);
            pushPort(vm, sa);
        } else {
            jsrPushNull(vm);
            jsrPushNull(vm);
        }
    }

    if(nread > 0 || nread == UV_EOF) {
        jsrPushNumber(vm, 0);
    } else {
        jsrPushNumber(vm, nread);
    }

    if(jsrCall(vm, pushSockAddr ? 4 : 2) != JSR_SUCCESS) {
        EventLoop_addException(vm, -1);
    }

    jsrPopN(vm, 3);
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

    if(!tryGetEventLoopAndHandle(vm, handleMetadata->handleId, loopMetadata->loopId)) {
        return;
    }
    int handleSlot = jsrTop(vm);

    int callback = handleMetadata->callbacks[cbType];
    if(callback != -1) {
        if(!Handle_getCallback(vm, callback, false, handleSlot) ||
           (jsrCall(vm, 0) != JSR_SUCCESS)) {
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

    if(!tryGetEventLoopAndHandle(vm, handleMetadata->handleId, loopMetadata->loopId)) {
        return;
    }
    int loopSlot = jsrTop(vm) - 1;
    int handleSlot = jsrTop(vm);

    if(!jsrGetField(vm, loopSlot, M_LOOP_WALK_CALLBACK)) {
        jsrPopN(vm, 3);
        return;
    }

    jsrPushValue(vm, handleSlot);
    if(jsrCall(vm, 1) != JSR_SUCCESS) {
        jsrPrintStacktrace(vm, -1);
        jsrPop(vm);
    }

    jsrPopN(vm, 2);
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
    
    if(!dns_getCallback(vm, true, callbackId)) {
        freeaddrinfo(res);
        EventLoop_addException(vm, -1);
        jsrPopN(vm, 2);
        return;
    }

    jsrPushList(vm);

    struct addrinfo* info = res;
    while(info) {
        if(!pushAddr(vm, info->ai_addr)) {
            freeaddrinfo(res);
            EventLoop_addException(vm, -1);
            jsrPopN(vm, 3);
            return;
        }
        if(!pushPort(vm, info->ai_addr)) {
            freeaddrinfo(res);
            EventLoop_addException(vm, -1);
            jsrPopN(vm, 4);
            return;
        }
        jsrPushTuple(vm, 2);
        jsrListAppend(vm, -2);
        jsrPop(vm);
        info = info->ai_next;
    }
    freeaddrinfo(res);

    jsrPushNumber(vm, status);
    
    if(jsrCall(vm, 2) != JSR_SUCCESS) {
        EventLoop_addException(vm, -1);
    }

    jsrPopN(vm, 2);
}
