#ifndef DNS_H
#define DNS_H

#include <jstar/jstar.h>

bool dns_getAddrInfo(JStarVM* vm);

bool dns_getCallback(JStarVM* vm, bool unregister, int callbackId);

#endif
