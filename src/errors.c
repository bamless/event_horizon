#include "errors.h"

#include <jstar/jstar.h>
#include <mbedtls/error.h>
#include <uv.h>

void EventHorizonException_raise(JStarVM* vm, const char* err) {
    if(!jsrGetGlobal(vm, "event_horizon.errors", "EventHorizonException")) return;
    jsrPushString(vm, err);
    if(!jsrCall(vm, 1)) return;
    jsrRaiseException(vm, -1);
}

void LoopExecutionException_raise(JStarVM* vm, int exceptionsSlot) {
    // Normalise a negative (top-relative) slot to an absolute index before
    // any pushes shift the stack top.
    if(exceptionsSlot < 0) exceptionsSlot = jsrTop(vm) + exceptionsSlot + 1;
    if(!jsrGetGlobal(vm, "event_horizon.errors", "LoopExecutionException")) return;
    jsrPushValue(vm, exceptionsSlot);
    if(!jsrCall(vm, 1)) return;
    jsrRaiseException(vm, -1);
}

void StatusException_raise(JStarVM* vm, int status) {
    if(!jsrGetGlobal(vm, "event_horizon.errors", "StatusException")) return;
    jsrPushNumber(vm, status);
    if(!jsrCall(vm, 1)) return;
    jsrRaiseException(vm, -1);
}

void TLSException_raise(JStarVM* vm, int mbedRet) {
    char buf[256];
    mbedtls_strerror(mbedRet, buf, sizeof(buf));
    if(!jsrGetGlobal(vm, "event_horizon.errors", "TLSException")) return;
    jsrPushString(vm, buf);
    if(!jsrCall(vm, 1)) return;
    jsrRaiseException(vm, -1);
}

bool errors_strerror(JStarVM* vm) {
    JSR_CHECK(Number, 1, "status");
    jsrPushString(vm, uv_strerror(jsrGetNumber(vm, 1)));
    return true;
}

bool errors_init(JStarVM* vm) {
    if(!jsrGetGlobal(vm, NULL, "Enum")) return false;

    jsrPushTable(vm);
    int tableSlot = jsrTop(vm);

#define SET_ERRNO(name, _)                            \
    jsrPushString(vm, #name);                         \
    jsrPushNumber(vm, UV_##name);                     \
    if(!jsrSubscriptSet(vm, tableSlot)) return false; \
    jsrPop(vm);

    UV_ERRNO_MAP(SET_ERRNO)

    if(!jsrCall(vm, 1)) return false;
    if(!jsrSetGlobal(vm, NULL, "Errno")) return false;
    jsrPop(vm);

    jsrPushNull(vm);
    return true;
}
