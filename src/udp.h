#ifndef UDP_H
#define UDP_H

#include <jstar/jstar.h>

// class UDP
bool UDP_bind(JStarVM* vm);
bool UDP_connect(JStarVM* vm);
bool UDP_send(JStarVM* vm);
bool UDP_trySend(JStarVM* vm);
bool UDP_recvStart(JStarVM* vm);
bool UDP_recvStop(JStarVM* vm);
bool UDP_sockName(JStarVM* vm);
bool UDP_peerName(JStarVM* vm);
bool UDP_sendQueueSize(JStarVM* vm);
bool UDP_sendQueueCount(JStarVM* vm);
bool UDP_setMembership(JStarVM* vm);
bool UDP_setSourceMembership(JStarVM* vm);
bool UDP_setMulticastLoop(JStarVM* vm);
bool UDP_setMulticastTTL(JStarVM* vm);
bool UDP_setMulticastInterface(JStarVM* vm);
bool UDP_setBroadcast(JStarVM* vm);
// end

#endif
