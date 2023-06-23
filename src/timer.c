#include "timer.h"

#include <uv.h>

#include "callbacks.h"
#include "errors.h"
#include "handle.h"

bool Timer_start(JStarVM* vm) {
    JSR_CHECK(Int, 1, "timeout");
    JSR_CHECK(Int, 2, "repeat");
    JSR_CHECK(Function, 3, "callback");

    uv_timer_t* timer = (uv_timer_t*)Handle_getHandle(vm, 0);
    if(!timer) return false;

    if(!Handle_registerCallback(vm, 3, TIMER_CB, 0)) {
        return false;
    }

    int res = uv_timer_start(timer, &timerCallback, jsrGetNumber(vm, 1), jsrGetNumber(vm, 2));
    if(res < 0) {
        if(!Handle_unregisterCallback(vm, TIMER_CB, 0)) {
            return false;
        }
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool Timer_stop(JStarVM* vm) {
    uv_timer_t* timer = (uv_timer_t*)Handle_getHandle(vm, 0);
    if(!timer) return false;

    int res = uv_timer_stop(timer);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}
