#ifndef ERRORS_H
#define ERRORS_H

#include <jstar/jstar.h>

bool errors_strerror(JStarVM* vm);

// class EventHorizonException
void EventHorizonException_raise(JStarVM* vm, const char* err);
// end

// class LoopExecutionException
void LoopExecutionException_raise(JStarVM* vm, int exceptionsSlot);
// end

// calss StatusException
void StatusException_raise(JStarVM* vm, int status);
// end

#endif
