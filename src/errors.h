#ifndef ERRORS_H
#define ERRORS_H

#include <jstar/jstar.h>

bool errors_strerror(JStarVM* vm);

void EventHorizonException_raise(JStarVM* vm, const char* err);
void LoopExecutionException_raise(JStarVM* vm, int exceptionsSlot);
void StatusException_raise(JStarVM* vm, int status);

#endif
