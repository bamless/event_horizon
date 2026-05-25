#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <jstar/conf.h>
#include <jstar/jstar.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

typedef struct {
    const unsigned char* data;
    size_t len;
} RingBuf_Span;

typedef struct {
    size_t head;
    size_t tail;
    size_t capacityMask;
    unsigned char* buf;
} RingBuf;

void ringBufInit(RingBuf* r, size_t capacity);
void ringBufFree(RingBuf* r);
size_t ringBufLen(const RingBuf* r);
size_t ringBufFreeSpace(const RingBuf* r);
bool ringBufEmpty(const RingBuf* r);
bool ringBufFull(const RingBuf* r);
size_t ringBufWrite(RingBuf* r, const unsigned char* src, size_t n);
size_t ringBufRead(RingBuf* r, unsigned char* dst, size_t n);
void ringBufSpans(const RingBuf* r, RingBuf_Span spans[static 2]);
void ringBufConsume(RingBuf* r, size_t n);

#endif
