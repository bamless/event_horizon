#ifndef ERRORS_H
#define ERRORS_H

#include <jstar/jstar.h>

void EventHorizonException_raise(JStarVM* vm, const char* err);
void LoopExecutionException_raise(JStarVM* vm, int exceptionsSlot);
void StatusException_raise(JStarVM* vm, int status);
// Raises a TLSException whose message is the human-readable mbedTLS error
// string for `mbedRet` (a negative mbedtls_* return value).
void TLSException_raise(JStarVM* vm, int mbedRet);

bool errors_strerror(JStarVM* vm);
bool errors_init(JStarVM* vm);

#endif
