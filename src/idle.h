#ifndef IDLE_H
#define IDLE_H

#include <jstar/jstar.h>

// class Idle
bool Idle_start(JStarVM* vm);
bool Idle_stop(JStarVM* vm);
// end

bool uvIdle(JStarVM* vm);

#endif
