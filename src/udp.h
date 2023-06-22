#ifndef UDP_H
#define UDP_H

#include <jstar/jstar.h>

// class UDP
bool UDP_bind(JStarVM* vm);
bool UDP_connect(JStarVM* vm);
bool UDP_send(JStarVM* vm);
bool UDP_recvStart(JStarVM* vm);
bool UDP_recvStop(JStarVM* vm);
bool UDP_sockName(JStarVM* vm);
bool UDP_peerName(JStarVM* vm);
// end

#endif
