#ifndef TCP_H
#define TCP_H

#include <jstar/jstar.h>

// class TCP
bool TCP_connect(JStarVM* vm);
bool TCP_bind(JStarVM* vm);
bool TCP_sockName(JStarVM* vm);
bool TCP_peerName(JStarVM* vm);
// end

bool uvTCP(JStarVM* vm);

#endif
