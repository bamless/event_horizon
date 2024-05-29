#ifndef TIMER_H
#define TIMER_H

#include <jstar/jstar.h>

// class Timer
bool Timer_start(JStarVM* vm);
bool Timer_stop(JStarVM* vm);
bool Timer_again(JStarVM* vm);
bool Timer_setRepeat(JStarVM* vm);
bool Timer_repeat(JStarVM* vm);
bool Timer_dueIn(JStarVM* vm);
// end

bool uvTimer(JStarVM* vm);

#endif
