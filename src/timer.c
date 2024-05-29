#include "timer.h"

#include <stdint.h>
#include <uv.h>

#include "callbacks.h"
#include "errors.h"
#include "event_loop.h"
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

bool Timer_again(JStarVM* vm) {
    uv_timer_t* timer = (uv_timer_t*)Handle_getHandle(vm, 0);
    if(!timer) return false;

    int res = uv_timer_again(timer);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool Timer_setRepeat(JStarVM* vm) {
    JSR_CHECK(Int, 1, "repeat");

    uv_timer_t* timer = (uv_timer_t*)Handle_getHandle(vm, 0);
    if(!timer) return false;

    uv_timer_set_repeat(timer, jsrGetNumber(vm, 1));

    jsrPushNull(vm);
    return true;
}

bool Timer_repeat(JStarVM* vm) {
    uv_timer_t* timer = (uv_timer_t*)Handle_getHandle(vm, 0);
    if(!timer) return false;
    uint64_t repeat = uv_timer_get_repeat(timer);
    jsrPushNumber(vm, repeat);
    return true;
}

bool Timer_dueIn(JStarVM* vm) {
    uv_timer_t* timer = (uv_timer_t*)Handle_getHandle(vm, 0);
    if(!timer) return false;
    uint64_t dueIn = uv_timer_get_due_in(timer);
    jsrPushNumber(vm, dueIn);
    return true;
}

bool uvTimer(JStarVM* vm) {
    uv_timer_t* handle = (uv_timer_t*)pushLibUVHandle(vm, sizeof(uv_timer_t));
    uv_loop_t* loop = EventLoop_getUVLoop(vm, 1);
    if(!loop) return false;
    uv_timer_init(loop, handle);
    return true;
}
