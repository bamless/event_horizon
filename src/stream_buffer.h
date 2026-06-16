#ifndef STREAM_BUFFER_H
#define STREAM_BUFFER_H

#include <jstar/jstar.h>
#include <stddef.h>

// class StreamBuffer
typedef struct {
    size_t headOffset;  // bytes consumed from the front chunk (not yet popped)
    size_t totalBytes;  // total buffered bytes across all chunks
} StreamBufState;

bool StreamBuffer_pushBack(JStarVM* vm);
bool StreamBuffer_drainN(JStarVM* vm);
bool StreamBuffer_findSepEnd(JStarVM* vm);
bool StreamBuffer_drainAll(JStarVM* vm);
bool StreamBuffer_len(JStarVM* vm);
bool StreamBuffer_init(JStarVM* vm);
// end

#endif
