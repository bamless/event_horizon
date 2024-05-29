#ifndef PIPE_H
#define PIPE_H

#include <jstar/jstar.h>

// class Pipe
bool Pipe_open(JStarVM* vm);
bool Pipe_bind(JStarVM* vm);
bool Pipe_connect(JStarVM* vm);
bool Pipe_sockName(JStarVM* vm);
bool Pipe_peerName(JStarVM* vm);
// end

bool uvPipe(JStarVM* vm);

#endif
