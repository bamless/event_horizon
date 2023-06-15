#ifndef STREAM_H
#define STREAM_H

#include <jstar/jstar.h>

// class Stream
bool Stream_write(JStarVM* vm);
bool Stream_tryWrite(JStarVM* vm);
bool Stream_readStart(JStarVM* vm);
bool Stream_readStop(JStarVM* vm);
bool Stream_shutdown(JStarVM* vm);
bool Stream_isReadable(JStarVM* vm);
bool Stream_isWritable(JStarVM* vm);
bool Stream_getWriteQueueSize(JStarVM* vm);
bool Stream_rawListen(JStarVM* vm);
bool Stream_rawAccept(JStarVM* vm);
// end

#endif
