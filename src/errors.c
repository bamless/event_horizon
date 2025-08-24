#include "errors.h"

#include <uv.h>

void EventHorizonException_raise(JStarVM* vm, const char* err) {
    if(!jsrGetGlobal(vm, "event_horizon.errors", "EventHorizonException")) return;
    jsrPushString(vm, err);
    if(!jsrCall(vm, 1)) return;
    jsrRaiseException(vm, -1);
}

void LoopExecutionException_raise(JStarVM* vm, int exceptionsSlot) {
    if(!jsrGetGlobal(vm, "event_horizon.errors", "LoopExecutionException")) return;
    if(exceptionsSlot < 0) exceptionsSlot -= 1;
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

bool errors_strerror(JStarVM* vm) {
    JSR_CHECK(Number, 1, "status");
    jsrPushString(vm, uv_strerror(jsrGetNumber(vm, 1)));
    return true;
}
