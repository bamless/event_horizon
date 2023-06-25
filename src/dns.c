#include "dns.h"

#include <string.h>
#include <uv.h>

#include "event_horizon.h"
#include "callbacks.h"
#include "event_loop.h"
#include "errors.h"

#define G_CALLBACKS "_callbacks"

static bool getRegistry(JStarVM* vm) {
    if(!jsrGetGlobal(vm, "event_horizon.uv.dns", G_CALLBACKS))  return false;
    if(jsrCall(vm, 0) != JSR_SUCCESS)  return false;
    return true;
}

static int registerCallback(JStarVM* vm, int callbackSlot) {
    if(!getRegistry(vm)) return -1;
    jsrPushValue(vm, callbackSlot);
    if(jsrCallMethod(vm, "ref", 1) != JSR_SUCCESS) return -1;
    int callbackId = jsrGetNumber(vm, -1);
    jsrPop(vm);
    return callbackId;
}

static bool unregisterCallback(JStarVM* vm, int callbackId) {
    if(!getRegistry(vm)) return false;
    jsrPushNumber(vm, callbackId);
    if(jsrCallMethod(vm, "unref", 1) != JSR_SUCCESS) return false;
    jsrPop(vm);
    return true;
}

bool dns_getCallback(JStarVM* vm, bool unregister, int callbackId) {
    if(!getRegistry(vm)) return false;
    jsrPushNumber(vm, callbackId);
    if(jsrCallMethod(vm, "get", 1) != JSR_SUCCESS) return false;
    if(unregister && !unregisterCallback(vm, callbackId)) {
        return false;
    }
    return true;
}

bool dns_getAddrInfo(JStarVM* vm) {
    JSR_CHECK(Function, 1, "callback");
    if(!jsrIsNull(vm, 2)) {
        JSR_CHECK(String, 2, "node");
    }
    if(!jsrIsNull(vm, 3)) {
        JSR_CHECK(String, 3, "service");
    }
    JSR_CHECK(Int, 4, "flags");
    JSR_CHECK(Int, 5, "family");
    JSR_CHECK(Int, 6, "sockType");
    JSR_CHECK(Int, 7, "protocol");

    uv_loop_t* loop;
    if(jsrIsNull(vm, 8)) {
        if(!jsrGetGlobal(vm, "event_horizon.uv", "loop")) return false;
        if(jsrCall(vm, 0) != JSR_SUCCESS) return false;
        loop = EventLoop_getUVLoop(vm, -1);
        jsrPop(vm);
    } else {
        loop = EventLoop_getUVLoop(vm, 8);
    }

    if(!loop) return false;

    int callbackId = registerCallback(vm, 1);
    if(callbackId == -1) return false;

    const char* node = jsrIsNull(vm, 2) ? NULL : jsrGetString(vm, 2);
    const char* service = jsrIsNull(vm, 3) ? NULL : jsrGetString(vm, 3);
    int flags = jsrGetNumber(vm, 4);
    int family = jsrGetNumber(vm, 5);
    int sockType = jsrGetNumber(vm, 6);
    int protocol = jsrGetNumber(vm, 7);

    struct addrinfo hints = (struct addrinfo) {
        .ai_flags = flags,
        .ai_family = family,
        .ai_socktype = sockType,
        .ai_protocol = protocol,
    };

    uv_getaddrinfo_t* req = malloc(sizeof(*req));
    setRequestCallback((uv_req_t*)req, callbackId);

    int res = uv_getaddrinfo(loop, req, &getAddrInfoCallback, node, service, &hints);
    if(res < 0) {
        free(req);
        if(!unregisterCallback(vm, callbackId)) {
            return false;
        }
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}
