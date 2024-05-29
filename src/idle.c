#include "idle.h"

#include <uv.h>

#include "callbacks.h"
#include "errors.h"
#include "event_loop.h"
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

bool uvIdle(JStarVM* vm) {
    uv_idle_t* handle = (uv_idle_t*)pushLibUVHandle(vm, sizeof(uv_idle_t));
    uv_loop_t* loop = EventLoop_getUVLoop(vm, 1);
    if(!loop) return false;
    uv_idle_init(loop, handle);
    return true;
}
