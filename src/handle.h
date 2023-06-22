#ifndef HANDLE_H
#define HANDLE_H

#include <jstar/jstar.h>
#include <uv.h>

#include "event_horizon.h"

// class Handle
#define M_HANDLE_LOOP      "_loop"
#define M_HANDLE_HANDLE    "_handle"
#define M_HANDLE_CALLBACKS "_callbacks"

// Native methods
bool Handle_init(JStarVM* vm);
bool Handle_close(JStarVM* vm);
bool Handle_isActive(JStarVM* vm);
bool Handle_isClosing(JStarVM* vm);
bool Handle_sendBufferSize(JStarVM* vm);
bool Handle_setSendBufferSize(JStarVM* vm);
bool Handle_recvBufferSize(JStarVM* vm);
bool Handle_setRecvBufferSize(JStarVM* vm);

// Uiility methods to be called from natives
bool Handle_getEventLoop(JStarVM* vm, int handleSlot);
uv_handle_t* Handle_getHandle(JStarVM* vm, int handleSlot);
bool Handle_registerCallback(JStarVM* vm, int callbackSlot, CallbackType type, int handleSlot);
int Handle_registerCallbackWithId(JStarVM* vm, int callbackSlot, int handleSlot);
bool Handle_getCallback(JStarVM* vm, int callbackId, bool unregister, int handleSlot);
bool Handle_unregisterCallback(JStarVM* vm, CallbackType type, int handleSlot);
bool Handle_unregisterCallbackById(JStarVM* vm, int callbackId, int handleSlot);
bool Handle_checkClosing(JStarVM* vm, int handleSlot);
// end

#endif
