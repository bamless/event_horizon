#include "consts.h"

#include <uv.h>

#define SET_ENUM_CONST(name) do { \
    jsrPushNumber(vm, name);      \
    jsrSetField(vm, -2, #name);   \
    jsrPop(vm);                   \
} while(0)

bool consts_init(JStarVM* vm) {
    if(!jsrGetGlobal(vm, NULL, "Family")) return false;
    SET_ENUM_CONST(AF_UNSPEC);
    SET_ENUM_CONST(AF_INET);
    SET_ENUM_CONST(AF_INET6);
    jsrPop(vm);

    if(!jsrGetGlobal(vm, NULL, "SockType")) return false;
    SET_ENUM_CONST(SOCK_STREAM);
    SET_ENUM_CONST(SOCK_DGRAM);
    jsrPop(vm);

    if(!jsrGetGlobal(vm, NULL, "AIFlags")) return false;
    SET_ENUM_CONST(AI_PASSIVE);
    SET_ENUM_CONST(AI_CANONNAME);
    SET_ENUM_CONST(AI_NUMERICHOST);
    SET_ENUM_CONST(AI_V4MAPPED);
    SET_ENUM_CONST(AI_ALL);
    SET_ENUM_CONST(AI_ADDRCONFIG);
# ifdef __USE_GNU
    SET_ENUM_CONST(AI_IDN);
    SET_ENUM_CONST(AI_CANONIDN);
#endif
    SET_ENUM_CONST(AI_NUMERICSERV);
    jsrPop(vm);

    jsrPushNull(vm);
    return true;
}
