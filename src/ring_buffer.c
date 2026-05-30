#include "ring_buffer.h"

#include <jstar/conf.h>
#include <string.h>

void ringBufInit(RingBuf* r, size_t capacity) {
    JSR_ASSERT((capacity & (capacity - 1)) == 0, "cap must be a power of 2");
    r->buf = malloc(capacity);
    JSR_ASSERT(r->buf, "Out of memory");
    r->capacityMask = capacity - 1;
    r->head = r->tail = 0;
}

void ringBufFree(RingBuf* r) {
    free(r->buf);
    r->buf = NULL;
}

size_t ringBufLen(const RingBuf* r) {
    return r->head - r->tail;
}

size_t ringBufAvailable(const RingBuf* r) {
    return r->capacityMask + 1 - ringBufLen(r);
}

bool ringBufEmpty(const RingBuf* r) {
    return r->head == r->tail;
}

bool ringBufFull(const RingBuf* r) {
    return ringBufLen(r) == r->capacityMask + 1;
}

size_t ringBufWrite(RingBuf* r, const unsigned char* src, size_t n) {
    size_t space = ringBufAvailable(r);
    if(n > space) n = space;
    size_t off = r->head & r->capacityMask;
    size_t first = r->capacityMask + 1 - off;
    if(first > n) first = n;
    memcpy(r->buf + off, src, first);
    if(n > first) memcpy(r->buf, src + first, n - first);
    r->head += n;
    return n;
}

size_t ringBufRead(RingBuf* r, unsigned char* dst, size_t n) {
    size_t avail = ringBufLen(r);
    if(n > avail) n = avail;
    size_t off = r->tail & r->capacityMask;
    size_t first = r->capacityMask + 1 - off;
    if(first > n) first = n;
    memcpy(dst, r->buf + off, first);
    if(n > first) memcpy(dst + first, r->buf, n - first);
    r->tail += n;
    return n;
}

void ringBufSpans(const RingBuf* r, RingBuf_Span spans[static 2]) {
    size_t avail = ringBufLen(r);
    size_t off = r->tail & r->capacityMask;
    size_t first = r->capacityMask + 1 - off;
    if(first >= avail) {
        spans[0].data = r->buf + off;
        spans[0].len = avail;
        spans[1].data = NULL;
        spans[1].len = 0;
    } else {
        spans[0].data = r->buf + off;
        spans[0].len = first;
        spans[1].data = r->buf;
        spans[1].len = avail - first;
    }
}

void ringBufConsume(RingBuf* r, size_t n) {
    JSR_ASSERT(n <= ringBufLen(r), "Not enough bytes in ring buffer");
    r->tail += n;
}
