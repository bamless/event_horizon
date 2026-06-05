#include "tls.h"

#include <jstar/conf.h>
#include <mbedtls/build_info.h>
#include <mbedtls/error.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#if MBEDTLS_VERSION_NUMBER < 0x04000000
    #include <mbedtls/ctr_drbg.h>
    #include <mbedtls/entropy.h>
#else
    #include <psa/crypto.h>
#endif

#include "callbacks.h"
#include "errors.h"
#include "event_loop.h"
#include "handle.h"
#include "ring_buffer.h"
#include "sock_utils.h"

#define TLS_CIPHER_IN_SIZE   (64 * 1024)
#define TLS_CIPHER_OUT_SIZE  (64 * 1024)
#define TLS_PLAIN_READ_CHUNK (16 * 1024)

typedef struct TLSWrite {
    const unsigned char* data;
    size_t len;
    size_t consumed;
    int callbackId;
    int dataRef;
    struct TLSWrite* next;
} TLSWrite;

typedef enum TLSState {
    TLS_STATE_HANDSHAKING,
    TLS_STATE_CONNECTED,
    TLS_STATE_SHUTDOWN_PENDING,
    TLS_STATE_SHUTDOWN_SENT,
    TLS_STATE_CLOSE_NOTIFY_PENDING,
    TLS_STATE_CLOSE_DRAINING,
    TLS_STATE_FATAL,
} TLSState;

// Keep struct and typedef name consistent with libuv.
typedef struct uv_tls_s {
    uv_tcp_t tcp;

    // mbedTLS contexts are initialized in uvTLS() and released with the J*
    // userdata. Closing the libuv handle only ends I/O; the userdata still owns
    // the TLS configuration until the VM collects it.
    //
    // TODO: Only `ssl` is genuinely per-connection. The config (`conf`), the
    // parsed certificates/key (`caCert`, `ownCert`, `pk`) and the RNG/entropy
    // contexts are derived solely from the TLS options and are identical for
    // every connection sharing those options. Today each handle re-parses the
    // cert/key files and re-seeds the RNG -- notably once per accepted server
    // client. These should live in a shared, refcounted config object (keyed by
    // the options) that all handles point at, leaving only `ssl` here.
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt caCert;
    mbedtls_x509_crt ownCert;
    mbedtls_pk_context pk;
#if MBEDTLS_VERSION_NUMBER < 0x04000000
    mbedtls_ctr_drbg_context ctrDrbg;
    mbedtls_entropy_context entropy;
#endif

    TLSState state;

    RingBuf cipherIn;
    RingBuf cipherOut;

    TLSWrite* writeHead;
    TLSWrite* writeTail;

    int handshakeCbId;
    int shutdownCbId;
    int lastTlsErr;
    int fatalStatus;

    bool rawReadActive;
    bool writeInFlight;
    bool peerClosed;
    bool drainArmed;
} uv_tls_t;

static void tlsPump(uv_tls_t* tls);
static void tlsAllocCallback(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf);
static void tlsRawReadCallback(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
static void tlsWriteCallback(uv_write_t* req, int status);
static void tlsShutdownCallback(uv_shutdown_t* req, int status);

// ------------------------------------------------------------------------------
// TLSHandle lifecycle
// ------------------------------------------------------------------------------

static void freeWriteQueue(uv_tls_t* tls) {
    TLSWrite* write = tls->writeHead;
    while(write) {
        TLSWrite* next = write->next;
        free(write);
        write = next;
    }
    tls->writeHead = NULL;
    tls->writeTail = NULL;
}

static void freeTLSHandle(void* data) {
    uv_tls_t* tls = data;
    freeWriteQueue(tls);
    ringBufFree(&tls->cipherIn);
    ringBufFree(&tls->cipherOut);
    mbedtls_ssl_free(&tls->ssl);
    mbedtls_ssl_config_free(&tls->conf);
    mbedtls_x509_crt_free(&tls->caCert);
    mbedtls_x509_crt_free(&tls->ownCert);
    mbedtls_pk_free(&tls->pk);
#if MBEDTLS_VERSION_NUMBER < 0x04000000
    mbedtls_ctr_drbg_free(&tls->ctrDrbg);
    mbedtls_entropy_free(&tls->entropy);
#endif
    free(tls->tcp.data);
}

static uv_tls_t* pushTLSHandle(JStarVM* vm) {
    jsrPushUserdata(vm, sizeof(uv_tls_t), &freeTLSHandle);
    uv_tls_t* tls = jsrGetUserdata(vm, -1);
    memset(tls, 0, sizeof(*tls));

    HandleMetadata* meta = malloc(sizeof(*meta));
    JSR_ASSERT(meta, "Out of memory");
    for(int i = 0; i < NUM_CB; i++) {
        meta->callbacks[i] = -1;
    }
    meta->handleId = -1;
    tls->tcp.data = meta;

    return tls;
}

// ------------------------------------------------------------------------------
// Queue helpers
// ------------------------------------------------------------------------------

static void enqueueWrite(uv_tls_t* tls, TLSWrite* write) {
    write->next = NULL;
    if(tls->writeTail) {
        tls->writeTail->next = write;
    } else {
        tls->writeHead = write;
    }
    tls->writeTail = write;
}

static TLSWrite* dequeueWrite(uv_tls_t* tls) {
    TLSWrite* write = tls->writeHead;
    if(!write) return NULL;

    tls->writeHead = write->next;
    if(!tls->writeHead) tls->writeTail = NULL;
    write->next = NULL;
    return write;
}

static void completeHeadWrite(uv_tls_t* tls, int status) {
    TLSWrite* write = dequeueWrite(tls);
    if(!write) return;

    int callbackId = write->callbackId;
    int dataRef = write->dataRef;
    free(write);

    reqCallback((uv_handle_t*)tls, callbackId, true, dataRef, status);
}

static void failWriteQueue(uv_tls_t* tls, int status) {
    while(tls->writeHead) {
        completeHeadWrite(tls, status);
    }
}

// ------------------------------------------------------------------------------
// Error and flow-control helpers
// ------------------------------------------------------------------------------

static bool hasReadCallback(uv_tls_t* tls) {
    HandleMetadata* meta = tls->tcp.data;
    return meta->callbacks[READ_CB] != -1;
}

static bool canReadPlaintext(uv_tls_t* tls) {
    return tls->state == TLS_STATE_CONNECTED || tls->state == TLS_STATE_SHUTDOWN_PENDING ||
           tls->state == TLS_STATE_SHUTDOWN_SENT;
}

static bool isCloseInProgress(uv_tls_t* tls) {
    return tls->state == TLS_STATE_CLOSE_NOTIFY_PENDING || tls->state == TLS_STATE_CLOSE_DRAINING;
}

static void stopRawRead(uv_tls_t* tls) {
    if(tls->rawReadActive) {
        uv_read_stop((uv_stream_t*)&tls->tcp);
        tls->rawReadActive = false;
    }
}

static int startRawRead(uv_tls_t* tls) {
    if(tls->rawReadActive || uv_is_closing((uv_handle_t*)tls)) return 0;

    int res = uv_read_start((uv_stream_t*)&tls->tcp, tlsAllocCallback, tlsRawReadCallback);
    if(res == 0 || res == UV_EALREADY) {
        tls->rawReadActive = true;
        return 0;
    }
    return res;
}

static void stopOnFatalStatus(uv_tls_t* tls, int status) {
    tls->fatalStatus = status;
    if(!isCloseInProgress(tls)) {
        tls->state = TLS_STATE_FATAL;
    }
    stopRawRead(tls);
    failWriteQueue(tls, status);
}

static void reportFatalStatus(uv_tls_t* tls, int status) {
    uv_handle_t* handle = (uv_handle_t*)tls;
    if(tls->handshakeCbId != -1) {
        int cbId = tls->handshakeCbId;
        tls->handshakeCbId = -1;
        reqCallback(handle, cbId, true, -1, status);
    } else if(!uv_is_closing(handle) && hasReadCallback(tls)) {
        deliverRead(handle, NULL, status);
    }
}

// ------------------------------------------------------------------------------
// mbedTLS BIO callbacks
// ------------------------------------------------------------------------------

static int sslSendCallback(void* ctx, const unsigned char* buf, size_t len) {
    uv_tls_t* tls = ctx;
    size_t written = ringBufWrite(&tls->cipherOut, buf, len);
    if(written == 0) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return (int)written;
}

static int sslRecvCallback(void* ctx, unsigned char* buf, size_t len) {
    uv_tls_t* tls = ctx;
    size_t read = ringBufRead(&tls->cipherIn, buf, len);
    if(read == 0) return tls->peerClosed ? 0 : MBEDTLS_ERR_SSL_WANT_READ;
    return (int)read;
}

// ------------------------------------------------------------------------------
// Ciphertext flushing
// ------------------------------------------------------------------------------

static int flushCiphertext(uv_tls_t* tls) {
    if(tls->writeInFlight || ringBufEmpty(&tls->cipherOut)) return 0;

    RingBuf_Span spans[2];
    ringBufSpans(&tls->cipherOut, spans);

    uv_buf_t bufs[2];
    int nbufs = 0;
    size_t total = 0;
    if(spans[0].data) {
        bufs[nbufs++] = uv_buf_init((char*)spans[0].data, spans[0].len);
        total += spans[0].len;
    }
    if(spans[1].data) {
        bufs[nbufs++] = uv_buf_init((char*)spans[1].data, spans[1].len);
        total += spans[1].len;
    }

    uv_write_t* req = malloc(sizeof(*req));
    JSR_ASSERT(req, "Out of memory");
    req->data = (void*)(uintptr_t)total;

    tls->writeInFlight = true;
    int res = uv_write(req, (uv_stream_t*)&tls->tcp, bufs, nbufs, tlsWriteCallback);
    if(res < 0) {
        free(req);
        tls->writeInFlight = false;
        stopOnFatalStatus(tls, res);
        return res;
    }

    return 0;
}

static void tlsWriteCallback(uv_write_t* req, int status) {
    uv_tls_t* tls = (uv_tls_t*)req->handle;
    size_t bytes = (size_t)(uintptr_t)req->data;
    free(req);

    tls->writeInFlight = false;

    if(status < 0) {
        bool closeWasInProgress = isCloseInProgress(tls);
        stopOnFatalStatus(tls, status);
        // A user-requested close was waiting for this write to drain. The write
        // failed, so there is nothing left to flush: honour the close now,
        // otherwise the handle would never be uv_close'd.
        if(closeWasInProgress && !uv_is_closing((uv_handle_t*)tls)) {
            uv_close((uv_handle_t*)tls, closeCallback);
        }
        return;
    }

    ringBufConsume(&tls->cipherOut, bytes);

    // The simplified write model deliberately keeps only one user write active
    // at a time. Once its plaintext is fully accepted by mbedTLS and all
    // generated ciphertext is out of our ring buffer, its J* callback follows
    // normal stream write semantics and fires with the libuv completion status.
    if(tls->writeHead && tls->writeHead->consumed == tls->writeHead->len &&
       ringBufEmpty(&tls->cipherOut)) {
        completeHeadWrite(tls, 0);
        if(uv_is_closing((uv_handle_t*)tls)) return;
    }

    if(tls->state == TLS_STATE_CLOSE_DRAINING && ringBufEmpty(&tls->cipherOut) &&
       !uv_is_closing((uv_handle_t*)tls)) {
        uv_close((uv_handle_t*)tls, closeCallback);
        return;
    }

    tlsPump(tls);
}

// ------------------------------------------------------------------------------
// Pump
// ------------------------------------------------------------------------------

static void pumpWrites(uv_tls_t* tls) {
    if(tls->writeInFlight || !tls->writeHead) return;

    // Encrypt queued plaintext one request at a time. This keeps write callback
    // ownership simple: the head request completes only after its last generated
    // ciphertext leaves our ring buffer.
    TLSWrite* write = tls->writeHead;
    while(write->consumed < write->len) {
        int ret = mbedtls_ssl_write(&tls->ssl, write->data + write->consumed,
                                    write->len - write->consumed);
        int flushStatus = flushCiphertext(tls);
        if(flushStatus < 0) {
            reportFatalStatus(tls, flushStatus);
            return;
        }

        if(ret > 0) {
            write->consumed += ret;
        } else if(ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            // TLS wants to read or write more data - quit the pump and wait for inbound
            // or outbound data.
            break;
        } else {
            tls->lastTlsErr = ret;
            stopOnFatalStatus(tls, UV_EPROTO);
            reportFatalStatus(tls, UV_EPROTO);
            break;
        }
    }
}

static void pumpReads(uv_tls_t* tls) {
    // TODO: below we use a loop with a fixed `TLS_PLAIN_READ_CHUNK` to get
    // data out of the possibly non-contigous ring buffer. This costs us an
    // extra copy into a staging buffer - profile to evaluate wether using a
    // memory-mapped contiguous ring wins us back some performance.

    // Deliver all available plaintext while the user read callback remains
    // installed. Callbacks may close the handle or stop reading, so re-check
    // state on every iteration.
    unsigned char plain[TLS_PLAIN_READ_CHUNK];
    while(canReadPlaintext(tls) && hasReadCallback(tls)) {
        size_t cipherBefore = ringBufLen(&tls->cipherIn);
        int ret = mbedtls_ssl_read(&tls->ssl, plain, sizeof(plain));
        size_t cipherAfter = ringBufLen(&tls->cipherIn);

        int flushStatus = flushCiphertext(tls);
        if(flushStatus < 0) {
            reportFatalStatus(tls, flushStatus);
            break;
        }

        if(ret > 0) {
            deliverRead((uv_handle_t*)tls, plain, ret);
        } else if(ret == MBEDTLS_ERR_SSL_WANT_READ) {
            // TLS needs more input. If our ciphertext ring still has bytes and the last call
            // consumed some, keep feeding it. If no progress was made we genuinely need
            // data from the network, so we quit the pump.
            if(cipherAfter > 0 && cipherAfter < cipherBefore) continue;
            else break;
        } else if(ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            // TLS wants to write some data - quit the pump
            break;
        } else if(ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
            // Safe to ignore - right now we do not support session tickets.
            // https://github.com/Mbed-TLS/mbedtls/issues/8749#issuecomment-2317146634
            continue;
        } else if(ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || ret == 0) {
            tls->peerClosed = true;
            deliverRead((uv_handle_t*)tls, NULL, UV_EOF);
            break;
        } else {
            tls->lastTlsErr = ret;
            stopOnFatalStatus(tls, UV_EPROTO);
            reportFatalStatus(tls, UV_EPROTO);
            return;
        }
    }
}

static void tlsPump(uv_tls_t* tls) {
    if(tls->fatalStatus < 0) return;

    switch(tls->state) {
    case TLS_STATE_HANDSHAKING: {
        // Advance the handshake. mbedTLS may emit handshake ciphertext on any result,
        // so flush before interpreting WANT_READ/WANT_WRITE as a wait.
        int ret = mbedtls_ssl_handshake(&tls->ssl);
        int flushStatus = flushCiphertext(tls);
        if(flushStatus < 0) {
            // flushCiphertext already recorded the fatal status, just report it.
            reportFatalStatus(tls, flushStatus);
            return;
        }

        if(ret == 0) {
            tls->state = TLS_STATE_CONNECTED;
            if(!hasReadCallback(tls)) stopRawRead(tls);
            if(tls->handshakeCbId != -1) {
                reqCallback((uv_handle_t*)tls, tls->handshakeCbId, true, -1, 0);
                tls->handshakeCbId = -1;
            }
        } else if(ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            // TODO: A cert verification failure arrives here as the generic
            // MBEDTLS_ERR_X509_CERT_VERIFY_FAILED, which mbedtls_strerror later
            // renders as "unknown error code" in tlsError(). The real reason is
            // only available right now, via mbedtls_ssl_get_verify_result(&ssl)
            // (a bitmask formatted by mbedtls_x509_crt_verify_info). When ret is
            // MBEDTLS_ERR_X509_CERT_VERIFY_FAILED we should capture that detail
            // here (e.g. the verify flags) so tlsError() can report it.
            tls->lastTlsErr = ret;
            stopOnFatalStatus(tls, UV_EPROTO);
            reportFatalStatus(tls, UV_EPROTO);
            return;
        }
    } break;
    case TLS_STATE_CONNECTED: {
        pumpWrites(tls);
        pumpReads(tls);
    } break;
    case TLS_STATE_SHUTDOWN_PENDING: {
        // mbedTLS treats close_notify like any other nonblocking write: WANT_WRITE
        // means this same call must be retried after pending ciphertext drains.
        int ret = mbedtls_ssl_close_notify(&tls->ssl);
        int flushStatus = flushCiphertext(tls);
        if(flushStatus < 0) {
            int callbackId = tls->shutdownCbId;
            tls->shutdownCbId = -1;
            reqCallback((uv_handle_t*)tls, callbackId, true, -1, flushStatus);
            return;
        }

        if(ret == 0) {
            tls->state = TLS_STATE_SHUTDOWN_SENT;
            int callbackId = tls->shutdownCbId;
            tls->shutdownCbId = -1;

            uv_shutdown_t* req = malloc(sizeof(*req));
            JSR_ASSERT(req, "Out of memory");
            setRequestCallback((uv_req_t*)req, callbackId);

            int res = uv_shutdown(req, (uv_stream_t*)&tls->tcp, tlsShutdownCallback);
            if(res < 0) {
                free(req);
                reqCallback((uv_handle_t*)tls, callbackId, true, -1, res);
            }
        } else if(ret == MBEDTLS_ERR_SSL_WANT_READ) {
            int res = startRawRead(tls);
            if(res < 0) {
                int callbackId = tls->shutdownCbId;
                tls->shutdownCbId = -1;
                stopOnFatalStatus(tls, res);
                reqCallback((uv_handle_t*)tls, callbackId, true, -1, res);
            }
        } else if(ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            int callbackId = tls->shutdownCbId;
            tls->shutdownCbId = -1;
            tls->lastTlsErr = ret;
            stopOnFatalStatus(tls, UV_EPROTO);
            reqCallback((uv_handle_t*)tls, callbackId, true, -1, UV_EPROTO);
        }
    } break;
    case TLS_STATE_SHUTDOWN_SENT: {
        pumpReads(tls);
    } break;
    case TLS_STATE_CLOSE_NOTIFY_PENDING: {
        // Final close is best-effort: flush any close_notify bytes mbedTLS
        // produced, but do not wait for peer input. Once user reads are
        // canceled, waiting on WANT_READ could keep close pending forever.
        int ret = mbedtls_ssl_close_notify(&tls->ssl);
        int flushStatus = flushCiphertext(tls);
        if(flushStatus < 0) {
            if(!tls->writeInFlight && !ringBufEmpty(&tls->cipherOut)) {
                ringBufConsume(&tls->cipherOut, ringBufLen(&tls->cipherOut));
            }
            tls->state = TLS_STATE_CLOSE_DRAINING;
            break;
        }

        if(ret == 0) {
            tls->state = TLS_STATE_CLOSE_DRAINING;
        } else if(ret == MBEDTLS_ERR_SSL_WANT_READ) {
            tls->state = TLS_STATE_CLOSE_DRAINING;
        } else if(ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            tls->lastTlsErr = ret;
            stopOnFatalStatus(tls, UV_EPROTO);
            tls->state = TLS_STATE_CLOSE_DRAINING;
        }
    } break;
    case TLS_STATE_CLOSE_DRAINING:
    case TLS_STATE_FATAL:
        break;
    }

    int flushStatus = flushCiphertext(tls);
    if(flushStatus < 0) reportFatalStatus(tls, flushStatus);

    // Finish a user-requested close once its ciphertext has drained. If the
    // drain flush failed synchronously the socket is dead and can never drain,
    // so close anyway rather than leaking the handle.
    if(tls->state == TLS_STATE_CLOSE_DRAINING && !tls->writeInFlight &&
       (ringBufEmpty(&tls->cipherOut) || flushStatus < 0) && !uv_is_closing((uv_handle_t*)tls)) {
        uv_close((uv_handle_t*)tls, closeCallback);
    }
}

// ------------------------------------------------------------------------------
// Raw socket reads
// ------------------------------------------------------------------------------

static void tlsAllocCallback(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf) {
    (void)suggestedSize;

    uv_tls_t* tls = (uv_tls_t*)handle;
    LoopMetadata* loop = handle->loop->data;
    size_t len = ringBufAvailable(&tls->cipherIn);
    if(len > LOOP_READ_BUF_SIZE) len = LOOP_READ_BUF_SIZE;

    if(len == 0) {
        buf->base = NULL;
        buf->len = 0;
    } else {
        buf->base = loop->readBuf;
        buf->len = len;
    }
}

static void tlsRawReadCallback(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    uv_tls_t* tls = (uv_tls_t*)stream;

    if(nread > 0) {
        size_t written = ringBufWrite(&tls->cipherIn, (const unsigned char*)buf->base,
                                      (size_t)nread);
        // The alloc callback caps the buffer to cipherIn's free space, so the
        // write should always fit; treat a short write as a fatal overflow.
        JSR_ASSERT(written == (size_t)nread, "Write should always fit the ring buffer");
        tlsPump(tls);
    } else if(nread == UV_EOF) {
        // Let the pump observe the peer close: the handshake will fail or the
        // read loop will surface EOF to the user.
        tls->peerClosed = true;
        tlsPump(tls);
    } else if(nread < 0) {
        // Any other negative nread (including UV_ENOBUFS from a zero-length
        // alloc) is a fatal socket error. Record it and report; the owner closes.
        stopOnFatalStatus(tls, nread);
        reportFatalStatus(tls, nread);
        if(isCloseInProgress(tls) && !uv_is_closing((uv_handle_t*)tls)) {
            uv_close((uv_handle_t*)tls, closeCallback);
        }
    }
}

// ------------------------------------------------------------------------------
// TCP connect
// ------------------------------------------------------------------------------

static void tlsTcpConnectedCallback(uv_connect_t* req, int status) {
    uv_tls_t* tls = (uv_tls_t*)req->handle;
    free(req);

    if(status < 0) {
        stopOnFatalStatus(tls, status);
        reportFatalStatus(tls, status);
        return;
    }

    int res = startRawRead(tls);
    if(res < 0) {
        stopOnFatalStatus(tls, res);
        reportFatalStatus(tls, res);
        return;
    }

    tlsPump(tls);
}

// ------------------------------------------------------------------------------
// Native implementations
// ------------------------------------------------------------------------------

bool TLS_bind(JStarVM* vm) {
    JSR_CHECK(String, 1, "addr");
    JSR_CHECK(Int, 2, "port");
    JSR_CHECK(Boolean, 3, "ipv6Only");

    uv_tcp_t* tcp = (uv_tcp_t*)Handle_getHandle(vm, 0);
    if(!tcp) return false;

    sockaddr_union sa;
    int res = initSockaddr(jsrGetString(vm, 1), (int)jsrGetNumber(vm, 2), &sa);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    unsigned int flags = jsrGetBoolean(vm, 3) ? UV_TCP_IPV6ONLY : 0;
    res = uv_tcp_bind(tcp, &sa.sa, flags);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool TLS_connect(JStarVM* vm) {
    JSR_CHECK(String, 1, "addr");
    JSR_CHECK(Int, 2, "port");
    if(!jsrIsNull(vm, 3)) JSR_CHECK(String, 3, "serverName");
    if(!jsrIsNull(vm, 4)) JSR_CHECK(Function, 4, "callback");
    if(!Handle_checkClosing(vm, 0)) return false;

    uv_tls_t* tls = (uv_tls_t*)Handle_getHandle(vm, 0);
    if(!tls) return false;
    if(tls->fatalStatus < 0) {
        StatusException_raise(vm, tls->fatalStatus);
        return false;
    }

    if(!jsrIsNull(vm, 3)) {
        int ret = mbedtls_ssl_set_hostname(&tls->ssl, jsrGetString(vm, 3));
        if(ret != 0) {
            TLSException_raise(vm, ret);
            return false;
        }
    }

    sockaddr_union sa;
    int res = initSockaddr(jsrGetString(vm, 1), (int)jsrGetNumber(vm, 2), &sa);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    if(!jsrIsNull(vm, 4)) {
        tls->handshakeCbId = Handle_registerCallbackWithId(vm, 4, 0);
        if(tls->handshakeCbId == -1) return false;
    }

    uv_connect_t* req = malloc(sizeof(*req));
    JSR_ASSERT(req, "Out of memory");

    res = uv_tcp_connect(req, &tls->tcp, &sa.sa, tlsTcpConnectedCallback);
    if(res < 0) {
        free(req);
        if(!Handle_unregisterCallbackById(vm, tls->handshakeCbId, 0)) return false;
        tls->handshakeCbId = -1;
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool TLS_startServerHandshake(JStarVM* vm) {
    JSR_CHECK(Function, 1, "callback");
    if(!Handle_checkClosing(vm, 0)) return false;

    uv_tls_t* tls = (uv_tls_t*)Handle_getHandle(vm, 0);
    if(!tls) return false;

    tls->handshakeCbId = Handle_registerCallbackWithId(vm, 1, 0);
    if(tls->handshakeCbId == -1) return false;

    int res = startRawRead(tls);
    if(res < 0) {
        int cbId = tls->handshakeCbId;
        tls->handshakeCbId = -1;
        if(!Handle_unregisterCallbackById(vm, cbId, 0)) return false;
        StatusException_raise(vm, res);
        return false;
    }

    tlsPump(tls);
    jsrPushNull(vm);
    return true;
}

bool TLS_readStart(JStarVM* vm) {
    JSR_CHECK(Function, 1, "callback");
    if(!Handle_checkClosing(vm, 0)) return false;

    uv_tls_t* tls = (uv_tls_t*)Handle_getHandle(vm, 0);
    if(!tls) return false;
    if(tls->fatalStatus < 0) {
        StatusException_raise(vm, tls->fatalStatus);
        return false;
    }
    if(isCloseInProgress(tls)) {
        StatusException_raise(vm, UV_EALREADY);
        return false;
    }

    if(!Handle_registerCallback(vm, 1, READ_CB, 0)) return false;
    int res = startRawRead(tls);
    if(res < 0) {
        if(!Handle_unregisterCallback(vm, READ_CB, 0)) return false;
        StatusException_raise(vm, res);
        return false;
    }

    tlsPump(tls);
    jsrPushNull(vm);
    return true;
}

bool TLS_readStop(JStarVM* vm) {
    uv_tls_t* tls = (uv_tls_t*)Handle_getHandle(vm, 0);
    if(!tls) return false;

    if(!Handle_unregisterCallback(vm, READ_CB, 0)) return false;
    if(canReadPlaintext(tls)) stopRawRead(tls);

    jsrPushNull(vm);
    return true;
}

bool TLS_write(JStarVM* vm) {
    JSR_CHECK(String, 1, "data");
    if(!jsrIsNull(vm, 2)) JSR_CHECK(Function, 2, "callback");
    if(!Handle_checkClosing(vm, 0)) return false;

    uv_tls_t* tls = (uv_tls_t*)Handle_getHandle(vm, 0);
    if(!tls) return false;
    if(tls->fatalStatus < 0) {
        StatusException_raise(vm, tls->fatalStatus);
        return false;
    }
    if(tls->state == TLS_STATE_HANDSHAKING) {
        EventHorizonException_raise(vm, "TLS handshake not complete");
        return false;
    }
    if(tls->state == TLS_STATE_SHUTDOWN_PENDING || tls->state == TLS_STATE_SHUTDOWN_SENT) {
        StatusException_raise(vm, UV_EPIPE);
        return false;
    }
    if(isCloseInProgress(tls)) {
        StatusException_raise(vm, UV_EALREADY);
        return false;
    }

    int callbackId = -1;
    if(!jsrIsNull(vm, 2)) {
        callbackId = Handle_registerCallbackWithId(vm, 2, 0);
        if(callbackId == -1) return false;
    }

    int dataRef = Handle_pushPending(vm, 1, 0);
    if(dataRef == -1) {
        if(!Handle_unregisterCallbackById(vm, callbackId, 0)) return false;
        return false;
    }

    const char* data = jsrGetString(vm, 1);
    size_t len = jsrGetStringSz(vm, 1);

    TLSWrite* write = malloc(sizeof(*write));
    JSR_ASSERT(write, "Out of memory");
    write->data = (const unsigned char*)data;
    write->len = len;
    write->consumed = 0;
    write->callbackId = callbackId;
    write->dataRef = dataRef;
    write->next = NULL;

    enqueueWrite(tls, write);
    tlsPump(tls);

    jsrPushNull(vm);
    return true;
}

bool TLS_tlsError(JStarVM* vm) {
    uv_tls_t* tls = (uv_tls_t*)Handle_getHandle(vm, 0);
    if(!tls) return false;

    if(tls->lastTlsErr == 0) {
        jsrPushNull(vm);
        return true;
    }

    char buf[256];
    mbedtls_strerror(tls->lastTlsErr, buf, sizeof(buf));
    tls->lastTlsErr = 0;
    jsrPushString(vm, buf);
    return true;
}

static void tlsShutdownCallback(uv_shutdown_t* req, int status) {
    int callbackId = getRequestCallback((uv_req_t*)req);
    uv_handle_t* handle = (uv_handle_t*)req->handle;
    free(req);
    reqCallback(handle, callbackId, true, -1, status);
}

bool TLS_shutdown(JStarVM* vm) {
    if(!jsrIsNull(vm, 1)) JSR_CHECK(Function, 1, "callback");
    if(!Handle_checkClosing(vm, 0)) return false;

    uv_tls_t* tls = (uv_tls_t*)Handle_getHandle(vm, 0);
    if(!tls) return false;
    if(tls->fatalStatus < 0) {
        StatusException_raise(vm, tls->fatalStatus);
        return false;
    }
    if(tls->state == TLS_STATE_HANDSHAKING) {
        EventHorizonException_raise(vm, "TLS handshake not complete");
        return false;
    }
    if(tls->state == TLS_STATE_SHUTDOWN_PENDING || tls->state == TLS_STATE_SHUTDOWN_SENT ||
       isCloseInProgress(tls)) {
        StatusException_raise(vm, UV_EALREADY);
        return false;
    }
    if(tls->writeHead) {
        EventHorizonException_raise(vm, "TLS shutdown requires all pending writes to complete");
        return false;
    }

    int callbackId = -1;
    if(!jsrIsNull(vm, 1)) {
        callbackId = Handle_registerCallbackWithId(vm, 1, 0);
        if(callbackId == -1) return false;
    }

    tls->shutdownCbId = callbackId;
    tls->state = TLS_STATE_SHUTDOWN_PENDING;
    tlsPump(tls);

    jsrPushNull(vm);
    return true;
}

bool TLS_close(JStarVM* vm) {
    if(!jsrIsNull(vm, 1)) JSR_CHECK(Function, 1, "callback");

    uv_tls_t* tls = (uv_tls_t*)Handle_getHandle(vm, 0);
    if(!tls) return false;
    if(uv_is_closing((uv_handle_t*)tls) || isCloseInProgress(tls)) {
        StatusException_raise(vm, UV_EALREADY);
        return false;
    }

    if(!jsrIsNull(vm, 1) && !Handle_registerCallback(vm, 1, CLOSE_CB, 0)) return false;

    TLSState previousState = tls->state;
    tls->state = TLS_STATE_CLOSE_DRAINING;

    // Closing owns the stream from this point on. User-facing reads and writes
    // are canceled; final close only waits for our own ciphertext to drain.
    stopRawRead(tls);
    cancelRead((uv_handle_t*)tls, UV_ECANCELED);
    failWriteQueue(tls, UV_ECANCELED);
    if(tls->shutdownCbId != -1) {
        int callbackId = tls->shutdownCbId;
        tls->shutdownCbId = -1;
        reqCallback((uv_handle_t*)tls, callbackId, true, -1, UV_ECANCELED);
    }

    if(tls->fatalStatus < 0 && !tls->writeInFlight && !ringBufEmpty(&tls->cipherOut)) {
        ringBufConsume(&tls->cipherOut, ringBufLen(&tls->cipherOut));
    }

    if((previousState == TLS_STATE_CONNECTED || previousState == TLS_STATE_SHUTDOWN_PENDING) &&
       tls->fatalStatus == 0) {
        tls->state = TLS_STATE_CLOSE_NOTIFY_PENDING;
        tlsPump(tls);
    } else {
        tls->state = TLS_STATE_CLOSE_DRAINING;
    }

    if(tls->state == TLS_STATE_CLOSE_DRAINING && ringBufEmpty(&tls->cipherOut) &&
       !tls->writeInFlight && !uv_is_closing((uv_handle_t*)tls)) {
        uv_close((uv_handle_t*)tls, closeCallback);
    } else if(tls->state != TLS_STATE_CLOSE_NOTIFY_PENDING) {
        tls->state = TLS_STATE_CLOSE_DRAINING;
    }

    jsrPushNull(vm);
    return true;
}

bool TLS_sockName(JStarVM* vm) {
    uv_tcp_t* tcp = (uv_tcp_t*)Handle_getHandle(vm, 0);
    if(!tcp) return false;

    sockaddr_union un;
    int len = sizeof(un);
    int res = uv_tcp_getsockname(tcp, &un.sa, &len);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    if(!pushAddr(vm, &un.sa)) return false;
    if(!pushPort(vm, &un.sa)) return false;
    jsrPushTuple(vm, 2);
    return true;
}

bool TLS_peerName(JStarVM* vm) {
    uv_tcp_t* tcp = (uv_tcp_t*)Handle_getHandle(vm, 0);
    if(!tcp) return false;

    sockaddr_union un;
    int len = sizeof(un);
    int res = uv_tcp_getpeername(tcp, &un.sa, &len);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    if(!pushAddr(vm, &un.sa)) return false;
    if(!pushPort(vm, &un.sa)) return false;
    jsrPushTuple(vm, 2);
    return true;
}

// slot 1: loop EventLoop
// slot 2: ca certificate path (String | null)
// slot 3: own certificate path (String | null)
// slot 4: private key path (String | null)
// slot 5: server mode flag (Boolean)
bool uvTLS(JStarVM* vm) {
    int tlsStatus;

    bool isServer = jsrGetBoolean(vm, 5);
    bool hasCert = !jsrIsNull(vm, 3);
    bool hasKey = !jsrIsNull(vm, 4);
    if(hasCert != hasKey) {
        JSR_RAISE(vm, "InvalidArgException", "TLS cert and key must be provided together");
        return false;
    }
    if(isServer && (!hasCert || !hasKey)) {
        JSR_RAISE(vm, "InvalidArgException", "TLS servers require both cert and key");
        return false;
    }

    uv_tls_t* tls = pushTLSHandle(vm);

    mbedtls_ssl_init(&tls->ssl);
    mbedtls_ssl_config_init(&tls->conf);
    mbedtls_x509_crt_init(&tls->caCert);
    mbedtls_x509_crt_init(&tls->ownCert);
    mbedtls_pk_init(&tls->pk);
#if MBEDTLS_VERSION_NUMBER < 0x04000000
    mbedtls_ctr_drbg_init(&tls->ctrDrbg);
    mbedtls_entropy_init(&tls->entropy);
#endif

    tls->state = TLS_STATE_HANDSHAKING;
    tls->handshakeCbId = -1;
    tls->shutdownCbId = -1;
    ringBufInit(&tls->cipherIn, TLS_CIPHER_IN_SIZE);
    ringBufInit(&tls->cipherOut, TLS_CIPHER_OUT_SIZE);

    uv_loop_t* loop = EventLoop_getUVLoop(vm, 1);
    if(!loop) return false;
    HandleMetadata* meta = tls->tcp.data;
    int uvRes = uv_tcp_init(loop, &tls->tcp);
    if(uvRes < 0) {
        StatusException_raise(vm, uvRes);
        return false;
    }
    uv_handle_set_data((uv_handle_t*)tls, meta);

#if MBEDTLS_VERSION_NUMBER < 0x04000000
    tlsStatus = mbedtls_ctr_drbg_seed(&tls->ctrDrbg, mbedtls_entropy_func, &tls->entropy, NULL, 0);
    if(tlsStatus != 0) goto tlsError;
#else
    psa_status_t psaRet = psa_crypto_init();
    if(psaRet != PSA_SUCCESS) {
        tlsStatus = MBEDTLS_ERR_SSL_INTERNAL_ERROR;
        goto tlsError;
    }
#endif

    int endpoint = isServer ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT;
    tlsStatus = mbedtls_ssl_config_defaults(&tls->conf, endpoint, MBEDTLS_SSL_TRANSPORT_STREAM,
                                            MBEDTLS_SSL_PRESET_DEFAULT);
    if(tlsStatus != 0) goto tlsError;

#if MBEDTLS_VERSION_NUMBER < 0x04000000
    mbedtls_ssl_conf_rng(&tls->conf, mbedtls_ctr_drbg_random, &tls->ctrDrbg);
#endif

    if(!jsrIsNull(vm, 2)) {
        tlsStatus = mbedtls_x509_crt_parse_file(&tls->caCert, jsrGetString(vm, 2));
        if(tlsStatus != 0) goto tlsError;
        mbedtls_ssl_conf_ca_chain(&tls->conf, &tls->caCert, NULL);
        mbedtls_ssl_conf_authmode(&tls->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    } else {
        mbedtls_ssl_conf_authmode(&tls->conf, MBEDTLS_SSL_VERIFY_NONE);
    }

    if(hasCert && hasKey) {
        tlsStatus = mbedtls_x509_crt_parse_file(&tls->ownCert, jsrGetString(vm, 3));
        if(tlsStatus != 0) goto tlsError;
#if MBEDTLS_VERSION_NUMBER < 0x04000000
        tlsStatus = mbedtls_pk_parse_keyfile(&tls->pk, jsrGetString(vm, 4), NULL,
                                             mbedtls_ctr_drbg_random, &tls->ctrDrbg);
#else
        tlsStatus = mbedtls_pk_parse_keyfile(&tls->pk, jsrGetString(vm, 4), NULL);
#endif
        if(tlsStatus != 0) goto tlsError;

        tlsStatus = mbedtls_ssl_conf_own_cert(&tls->conf, &tls->ownCert, &tls->pk);
        if(tlsStatus != 0) goto tlsError;
    }

    tlsStatus = mbedtls_ssl_setup(&tls->ssl, &tls->conf);
    if(tlsStatus != 0) goto tlsError;

    mbedtls_ssl_set_bio(&tls->ssl, tls, sslSendCallback, sslRecvCallback, NULL);
    return true;

tlsError:
    uv_close((uv_handle_t*)tls, NULL);
    TLSException_raise(vm, tlsStatus);
    return false;
}
