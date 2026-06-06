#ifndef PREPARE_H
#define PREPARE_H

#include <jstar/jstar.h>

// class Prepare
bool Prepare_start(JStarVM* vm);
bool Prepare_stop(JStarVM* vm);
// end

bool uvPrepare(JStarVM* vm);

#endif
