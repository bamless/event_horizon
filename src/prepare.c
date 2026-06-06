#include "prepare.h"

#include <uv.h>

#include "callbacks.h"
#include "errors.h"
#include "event_loop.h"
#include "handle.h"

bool Prepare_start(JStarVM* vm) {
    JSR_CHECK(Function, 1, "callback");

    uv_prepare_t* prepare = (uv_prepare_t*)Handle_getHandle(vm, 0);
    if(!prepare) return false;

    if(!Handle_registerCallback(vm, 1, PREPARE_CB, 0)) {
        return false;
    }

    int res = uv_prepare_start(prepare, &prepareCallback);
    if(res < 0) {
        if(!Handle_unregisterCallback(vm, PREPARE_CB, 0)) {
            return false;
        }
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool Prepare_stop(JStarVM* vm) {
    uv_prepare_t* prepare = (uv_prepare_t*)Handle_getHandle(vm, 0);
    if(!prepare) return false;

    if(!Handle_unregisterCallback(vm, PREPARE_CB, 0)) {
        return false;
    }

    uv_prepare_stop(prepare);
    jsrPushNull(vm);
    return true;
}

bool uvPrepare(JStarVM* vm) {
    uv_prepare_t* handle = (uv_prepare_t*)pushLibUVHandle(vm, sizeof(uv_prepare_t));
    uv_loop_t* loop = EventLoop_getUVLoop(vm, 1);
    if(!loop) return false;
    uv_prepare_init(loop, handle);
    return true;
}
