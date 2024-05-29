#include "idle.h"

#include <uv.h>

#include "callbacks.h"
#include "errors.h"
#include "handle.h"

bool Idle_start(JStarVM* vm) {
    JSR_CHECK(Function, 1, "callback");

    uv_idle_t* idle = (uv_idle_t*)Handle_getHandle(vm, 0);
    if(!idle) return false;

    if(!Handle_registerCallback(vm, 1, IDLE_CB, 0)) {
        return false;
    }

    int res = uv_idle_start(idle, &idleCallback);
    if(res < 0) {
        if(!Handle_unregisterCallback(vm, IDLE_CB, 0)) {
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

    if(!Handle_unregisterCallback(vm, IDLE_CB, 0)) {
        return false;
    }

    uv_idle_stop(idle);
    jsrPushNull(vm);
    return true;
}
