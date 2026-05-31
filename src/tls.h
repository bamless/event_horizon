#ifndef TLS_H
#define TLS_H

#include <jstar/jstar.h>

// class TLS is Stream
bool TLS_bind(JStarVM* vm);
bool TLS_connect(JStarVM* vm);
bool TLS_tlsError(JStarVM* vm);
bool TLS_sockName(JStarVM* vm);
bool TLS_peerName(JStarVM* vm);
bool TLS_readStart(JStarVM* vm);
bool TLS_readStop(JStarVM* vm);
bool TLS_rawWrite(JStarVM* vm);
bool TLS_pendingWriteQueueSize(JStarVM* vm);
bool TLS_shutdown(JStarVM* vm);
bool TLS_close(JStarVM* vm);
bool TLS_startServerHandshake(JStarVM* vm);
// end

bool uvTLS(JStarVM* vm);

#endif
