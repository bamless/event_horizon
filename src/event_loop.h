#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#include <jstar/jstar.h>
#include <stdbool.h>
#include <uv.h>

typedef struct LoopMetadata {
    JStarVM* vm;
    int loopId;
} LoopMetadata;

bool getEventLoopFromId(JStarVM* vm, int loopId);

// class EventLoop
#define M_LOOP_ID            "_id"
#define M_LOOP_CLOSED        "_closed"
#define M_LOOP_LOOP          "_loop"
#define M_LOOP_HANDLES       "_handles"
#define M_LOOP_WALK_CALLBACK "_walkCallback"

// Native methods
bool EventLoop_init(JStarVM* vm);
bool EventLoop_run(JStarVM* vm);
bool EventLoop_stop(JStarVM* vm);
bool EventLoop_alive(JStarVM* vm);
bool EventLoop_walk(JStarVM* vm);

// Utility methods to be called from natives
int EventLoop_getId(JStarVM* vm, int eventLoopSlot);
uv_loop_t* EventLoop_getUVLoop(JStarVM* vm, int eventLoopSlot);
int EventLoop_registerHandle(JStarVM* vm, int handleSlot, int eventLoopSlot);
bool EventLoop_unregisterHandle(JStarVM* vm, int handleId, int eventLoopSlot);
bool EventLoop_getHandle(JStarVM* vm, int handleId, int eventLoopSlot);
void EventLoop_addException(JStarVM* vm, int exceptionSlot);
// end

#endif
