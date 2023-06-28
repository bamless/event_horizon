#include "pipe.h"

#include <uv.h>

#include "errors.h"
#include "handle.h"
#include "sock_utils.h"

// class Pipe
bool Pipe_bind(JStarVM* vm) {
    JSR_CHECK(String, 1, "name");

    uv_pipe_t* pipe = (uv_pipe_t*)Handle_getHandle(vm, 0);
    if(!pipe) return false;

    int res = uv_pipe_bind(pipe, jsrGetString(vm, 1));
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}
//end
