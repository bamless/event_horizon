#include "stream_buffer.h"

#include <stddef.h>
#include <string.h>

// Separator length is bounded at compile time in order to allow static arrays for the boundary
// window buffer.
// Separators longer than this will be rejected with an InvalidArgException.
#define MAX_SEP_LEN 64

#define M_STATE  "_state"
#define M_CHUNKS "_chunks"

static JStarSymbol* sym_state;
static JStarSymbol* sym_chunks;
static JStarSymbol* sym_peek;
static JStarSymbol* sym_pop;
static JStarSymbol* sym_push;

static const char* findBytes(const char* hay, size_t haySz, const char* needle, size_t needleSz) {
    if(needleSz == 0) return hay;
    if(haySz < needleSz) return NULL;
    if(needleSz == 1) return (const char*)memchr(hay, (unsigned char)needle[0], haySz);
    for(size_t i = 0; i <= haySz - needleSz; i++) {
        if(memcmp(hay + i, needle, needleSz) == 0) return hay + i;
    }
    return NULL;
}

static StreamBufState* getState(JStarVM* vm, int self) {
    if(!jsrGetFieldCached(vm, self, M_STATE, sym_state)) return NULL;
    if(!jsrCheckUserdata(vm, -1, "StreamBuffer." M_STATE)) {
        jsrPop(vm);
        return NULL;
    }
    StreamBufState* s = jsrGetUserdata(vm, -1);
    jsrPop(vm);
    return s;
}

static bool peekFront(JStarVM* vm, int dequeSlot) {
    jsrPushValue(vm, dequeSlot);
    jsrPushNumber(vm, 0.0);
    return jsrCallMethodCached(vm, "peekFront", 1, sym_peek);
}

static bool popFront(JStarVM* vm, int dequeSlot) {
    jsrPushValue(vm, dequeSlot);
    if(!jsrCallMethodCached(vm, "popFront", 0, sym_pop)) return false;
    jsrPop(vm);
    return true;
}

static bool drainBytes(JStarVM* vm, int dequeSlot, StreamBufState* state, JStarBuffer* out,
                       size_t toDrain) {
    bool first = true;
    size_t remaining = toDrain;

    while(remaining > 0) {
        if(!peekFront(vm, dequeSlot)) return false;
        JSR_ASSERT(!jsrIsNull(vm, -1), "toDrain <= total_bytes guarantees a chunk is present");

        const char* data = jsrGetString(vm, -1);
        size_t len = jsrGetStringSz(vm, -1);

        size_t start = first ? state->headOffset : 0;
        JSR_ASSERT(start <= len, "head_offset must not exceed front chunk length");

        size_t available = len - start;
        first = false;

        if(available <= remaining) {
            jsrBufferAppend(out, data + start, available);
            remaining -= available;
            jsrPop(vm);  // chunk

            state->totalBytes -= available;
            state->headOffset = 0;

            if(!popFront(vm, dequeSlot)) return false;
        } else {
            jsrBufferAppend(out, data + start, remaining);
            jsrPop(vm);  // chunk

            state->headOffset = start + remaining;
            state->totalBytes -= remaining;
            remaining = 0;
        }
    }

    return true;
}

static bool findSepEnd(JStarVM* vm, int dequeSlot, StreamBufState* state, const char* sep,
                       size_t sepLen, ptrdiff_t* sepEnd) {
    // Scan for the separator across the chunk sequence.
    // logical_pos tracks the byte offset from the start of the logical buffer (i.e. from
    // head_offset of the first chunk). tail holds the last (sepLen-1) bytes seen so far
    // for cross-chunk boundary detection.
    *sepEnd = -1;
    size_t logicalPos = 0;
    char tail[MAX_SEP_LEN];
    size_t tailLen = 0;

    for(size_t i = 0;; i++) {
        jsrPushValue(vm, dequeSlot);
        jsrPushNumber(vm, (double)i);
        if(!jsrCallMethodCached(vm, "peekFront", 1, sym_peek)) return false;

        if(jsrIsNull(vm, -1)) {
            jsrPop(vm);
            break;
        }

        const char* raw = jsrGetString(vm, -1);
        size_t rawLen = jsrGetStringSz(vm, -1);
        jsrPop(vm);

        // Apply head_offset only to the first chunk.
        size_t off = i == 0 ? state->headOffset : 0;
        const char* chunk = raw + off;
        size_t chunkLen = rawLen - off;

        // Check whether the separator straddles the boundary between the previous data
        // (captured in tail) and the start of this chunk. A match is a boundary hit only
        // when it starts within the tail region (pos_in_window < tail_len). If it starts
        // inside this chunk's bytes the within-chunk search below will find it instead.
        if(tailLen > 0 && chunkLen > 0) {
            size_t headLen = (chunkLen < sepLen - 1) ? chunkLen : sepLen - 1;
            // window = tail + first headLen bytes of chunk; at most 2*(MAX_SEP_LEN-1) bytes.
            char window[MAX_SEP_LEN * 2 - 1];
            memcpy(window, tail, tailLen);
            memcpy(window + tailLen, chunk, headLen);
            size_t windowLen = tailLen + headLen;

            const char* found = findBytes(window, windowLen, sep, sepLen);
            if(found != NULL) {
                ptrdiff_t posInWindow = found - window;
                JSR_ASSERT(posInWindow >= 0, "findBytes returns a pointer within window");
                if((size_t)posInWindow < tailLen) {
                    *sepEnd = (ptrdiff_t)(logicalPos - tailLen) + posInWindow + (ptrdiff_t)sepLen;
                    break;
                }
            }
        }

        // Search for the separator entirely within this chunk.
        const char* found = findBytes(chunk, chunkLen, sep, sepLen);
        if(found != NULL) {
            JSR_ASSERT(found >= chunk, "findBytes returns a pointer within chunk");
            *sepEnd = logicalPos + (found - chunk) + sepLen;
            break;
        }

        logicalPos += chunkLen;

        // Advance the tail window: keep the last (sepLen-1) bytes of all data seen so far.
        // When chunks are shorter than sepLen-1 we accumulate across them so that a
        // separator split across three or more small chunks is still detected correctly.
        if(sepLen > 1) {
            size_t need = sepLen - 1;
            size_t combined = tailLen + chunkLen;
            size_t newTailLen = (combined < need) ? combined : need;

            if(newTailLen <= chunkLen) {
                // New tail comes entirely from this chunk.
                memcpy(tail, chunk + chunkLen - newTailLen, newTailLen);
            } else {
                // New tail is a suffix of old tail followed by all of this chunk.
                size_t fromTail = newTailLen - chunkLen;
                memmove(tail, tail + tailLen - fromTail, fromTail);
                memcpy(tail + fromTail, chunk, chunkLen);
            }
            tailLen = newTailLen;
        }
    }

    return true;
}

bool StreamBuffer_pushBack(JStarVM* vm) {
    JSR_CHECK(String, 1, "data");

    StreamBufState* state = getState(vm, 0);
    if(!state) return false;

    if(!jsrGetFieldCached(vm, 0, M_CHUNKS, sym_chunks)) return false;
    int dequeSlot = jsrTop(vm);

    jsrPushValue(vm, dequeSlot);
    jsrPushValue(vm, 1);
    if(!jsrCallMethodCached(vm, "pushBack", 1, sym_push)) {
        jsrPop(vm);  // deque
        return false;
    }
    jsrPop(vm);  // null return from pushBack
    jsrPop(vm);  // deque

    state->totalBytes += jsrGetStringSz(vm, 1);
    jsrPushNull(vm);
    return true;
}

bool StreamBuffer_drainN(JStarVM* vm) {
    JSR_CHECK(Int, 1, "n");

    StreamBufState* state = getState(vm, 0);
    if(!state) return false;

    size_t n = jsrGetNumber(vm, 1);

    if(state->totalBytes < n) {
        jsrPushNull(vm);
        return true;
    }

    if(!jsrGetFieldCached(vm, 0, M_CHUNKS, sym_chunks)) return false;
    int dequeSlot = jsrTop(vm);

    JStarBuffer out;
    jsrBufferInitCapacity(vm, &out, n);

    if(!drainBytes(vm, dequeSlot, state, &out, n)) {
        jsrBufferFree(&out);
        jsrPop(vm);  // deque
        return false;
    }

    jsrPop(vm);  // deque
    jsrBufferPush(&out);
    return true;
}

bool StreamBuffer_findSepEnd(JStarVM* vm) {
    JSR_CHECK(String, 1, "sep");

    const char* sep = jsrGetString(vm, 1);
    size_t sepLen = jsrGetStringSz(vm, 1);

    if(sepLen == 0) {
        JSR_RAISE(vm, "InvalidArgException", "separator must not be empty");
    }

    if(sepLen > MAX_SEP_LEN) {
        JSR_RAISE(vm, "InvalidArgException", "separator longer than MAX_SEP_LEN (%d bytes)",
                  MAX_SEP_LEN);
    }

    StreamBufState* state = getState(vm, 0);
    if(!state) return false;

    if(state->totalBytes == 0) {
        jsrPushNull(vm);
        return true;
    }

    if(!jsrGetFieldCached(vm, 0, M_CHUNKS, sym_chunks)) return false;
    int dequeSlot = jsrTop(vm);

    ptrdiff_t sepEnd = -1;
    if(!findSepEnd(vm, dequeSlot, state, sep, sepLen, &sepEnd)) {
        jsrPop(vm);  // deque
        return false;
    }

    jsrPop(vm);  // deque

    if(sepEnd < 0) {
        jsrPushNull(vm);
    } else {
        jsrPushNumber(vm, (double)sepEnd);
    }
    return true;
}

bool StreamBuffer_drainAll(JStarVM* vm) {
    StreamBufState* state = getState(vm, 0);
    if(!state) return false;

    if(state->totalBytes == 0) {
        jsrPushNull(vm);
        return true;
    }

    if(!jsrGetFieldCached(vm, 0, M_CHUNKS, sym_chunks)) return false;
    int dequeSlot = jsrTop(vm);

    JStarBuffer out;
    jsrBufferInitCapacity(vm, &out, state->totalBytes);

    bool first = true;
    for(;;) {
        if(!peekFront(vm, dequeSlot)) {
            jsrBufferFree(&out);
            jsrPop(vm);  // deque
            return false;
        }

        if(jsrIsNull(vm, -1)) {
            jsrPop(vm);
            break;
        }

        const char* data = jsrGetString(vm, -1);
        size_t len = jsrGetStringSz(vm, -1);
        size_t start = first ? state->headOffset : 0;
        JSR_ASSERT(start <= len, "head_offset must not exceed front chunk length");
        jsrBufferAppend(&out, data + start, len - start);
        first = false;
        jsrPop(vm);  // chunk

        if(!popFront(vm, dequeSlot)) {
            jsrBufferFree(&out);
            jsrPop(vm);  // deque
            return false;
        }
    }

    jsrPop(vm);  // deque

    state->headOffset = 0;
    state->totalBytes = 0;

    jsrBufferPush(&out);
    return true;
}

bool StreamBuffer_len(JStarVM* vm) {
    StreamBufState* state = getState(vm, 0);
    if(!state) return false;
    jsrPushNumber(vm, state->totalBytes);
    return true;
}

bool StreamBuffer_init(JStarVM* vm) {
    if(!sym_state) {
        sym_state = jsrNewSymbol(vm);
        sym_chunks = jsrNewSymbol(vm);
        sym_peek = jsrNewSymbol(vm);
        sym_pop = jsrNewSymbol(vm);
        sym_push = jsrNewSymbol(vm);
    }

    jsrPushUserdata(vm, sizeof(StreamBufState), NULL);
    StreamBufState* s = jsrGetUserdata(vm, -1);
    s->headOffset = 0;
    s->totalBytes = 0;

    jsrSetFieldCached(vm, 0, M_STATE, sym_state);
    jsrPop(vm);
    jsrPushNull(vm);
    return true;
}
