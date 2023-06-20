#include "idle.h"

#include <uv.h>

#include "callbacks.h"
#include "errors.h"
#include "event_horizon.h"
#include "handle.h"

bool Idle_start(JStarVM* vm) {
    JSR_CHECK(Function, 1, "callback");

    uv_idle_t* idle = (uv_idle_t*)Handle_getHandle(vm, 0);
    if(!idle) return false;
    HandleMetadata* metadata = idle->data;

    int callbackId = Handle_registerCallback(vm, 1, 0);
    if(callbackId == -1) return false;
    metadata->callbacks[IDLE_CB] = callbackId;

    int res = uv_idle_start(idle, &idleCallback);
    if(res < 0) {
        if(!Handle_unregisterCallback(vm, callbackId, 0)) {
            return false;
        }
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool Idle_stop(JStarVM* vm) {
    uv_idle_t* idle = (uv_idle_t*)Handle_getHandle(vm, 0);
    if(!idle) return false;
    uv_idle_stop(idle);
    jsrPushNull(vm);
    return true;
}
