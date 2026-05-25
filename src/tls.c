#include "tls.h"

#include <mbedtls/error.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

#include "callbacks.h"
#include "errors.h"
#include "event_loop.h"
#include "handle.h"
#include "sock_utils.h"

// ------------------------------------------------------------------------------
// TLSHandle lifecycle
// ------------------------------------------------------------------------------

static void freeTLSHandle(void* data) {
    TLSHandle* tls = data;
    mbedtls_ssl_free(&tls->ssl);
    mbedtls_ssl_config_free(&tls->conf);
    mbedtls_x509_crt_free(&tls->caCert);
    mbedtls_x509_crt_free(&tls->ownCert);
    mbedtls_pk_free(&tls->pk);
#if MBEDTLS_VERSION_NUMBER < 0x04000000
    mbedtls_ctr_drbg_free(&tls->ctrDrbg);
    mbedtls_entropy_free(&tls->entropy);
#endif
    HandleMetadata* meta = ((uv_handle_t*)tls)->data;
    free(meta);
}

// Mirrors pushLibUVHandle() but uses freeTLSHandle as the destructor so mbedTLS contexts are
// cleaned up on GC.
static TLSHandle* pushTLSHandle(JStarVM* vm) {
    jsrPushUserdata(vm, sizeof(TLSHandle), &freeTLSHandle);
    TLSHandle* tls = jsrGetUserdata(vm, -1);

    HandleMetadata* meta = malloc(sizeof(*meta));
    for(int i = 0; i < NUM_CB; i++) {
        meta->callbacks[i] = -1;
    }
    meta->handleId = -1;
    uv_handle_set_data((uv_handle_t*)tls, meta);

    return tls;
}

// ------------------------------------------------------------------------------
// Internal write request: ciphertext to network
// ------------------------------------------------------------------------------

// Carries one uv_write for ciphertext produced by mbedTLS. Two kinds:
//
//   isInternal=true  -> handshake message, no J* callback
//   isInternal=false -> user write, fires J* callback via reqCallback
//
// The struct itself is pooled (nextFree links the freelist); the ciphertext
// `data` buffer is still malloc'd per-write since its size varies.
typedef struct TLSWriteReq {
    uv_write_t req;
    bool isInternal;
    unsigned char* data;
    int callbackId;
    struct TLSWriteReq* nextFree;
} TLSWriteReq;

// Global pool of recycled TLSWriteReqs. Safe without locking: the J* VM is
// single-threaded and libuv callbacks fire on the same thread.
static TLSWriteReq* tlsWriteReqPool;

static TLSWriteReq* tlsWriteReqAcquire(void) {
    if(tlsWriteReqPool) {
        TLSWriteReq* wreq = tlsWriteReqPool;
        tlsWriteReqPool = wreq->nextFree;
        return wreq;
    }
    return malloc(sizeof(TLSWriteReq));
}

static void tlsWriteReqRelease(TLSWriteReq* wreq) {
    wreq->nextFree = tlsWriteReqPool;
    tlsWriteReqPool = wreq;
}

static void tlsWriteCallback(uv_write_t* req, int status) {
    TLSWriteReq* wreq = (TLSWriteReq*)req;
    free(wreq->data);
    if(wreq->isInternal) {
        tlsWriteReqRelease(wreq);
        return;
    }
    int cbId = wreq->callbackId;
    uv_handle_t* handle = (uv_handle_t*)req->handle;
    tlsWriteReqRelease(wreq);
    reqCallback(handle, cbId, true, -1, status);
}

// Flush accumulated ciphertext from sendStaging to libuv in a single uv_write.
// Must be called after every mbedtls_ssl_* operation that may have produced
// outgoing bytes. isInternal=true for handshake/protocol messages (no J*
// callback); false for user writes (cbId is forwarded to tlsWriteCallback).
static void tlsFlushSend(TLSHandle* tls, bool isInternal, int cbId) {
    if(tls->sendStagingLen == 0) return;

    TLSWriteReq* wreq = tlsWriteReqAcquire();
    wreq->data = malloc(tls->sendStagingLen);
    memcpy(wreq->data, tls->sendStaging, tls->sendStagingLen);
    wreq->isInternal = isInternal;
    wreq->callbackId = cbId;

    uv_buf_t buf = {(char*)wreq->data, tls->sendStagingLen};
    uv_write(&wreq->req, (uv_stream_t*)&tls->tcp, &buf, 1, tlsWriteCallback);
    tls->sendStagingLen = 0;
}

// ------------------------------------------------------------------------------
// mbedTLS memory BIO callbacks
// ------------------------------------------------------------------------------

// sslSendCallback - called synchronously by mbedTLS when it has ciphertext to
// send. We accumulate in sendStaging and flush to libuv after returning from
// the enclosing mbedtls_ssl_* call. This keeps the flush logic in one place
// and avoids multiple small uv_write calls per SSL operation.
static int sslSendCallback(void* ctx, const unsigned char* buf, size_t len) {
    TLSHandle* tls = ctx;
    size_t avail = TLS_SEND_STAGING_SIZE - tls->sendStagingLen;
    if(avail == 0) return MBEDTLS_ERR_SSL_WANT_WRITE;
    size_t n = len < avail ? len : avail;
    memcpy(tls->sendStaging + tls->sendStagingLen, buf, n);
    tls->sendStagingLen += n;
    return (int)n;
}

// sslRecvCallback - called synchronously by mbedTLS when it needs ciphertext to
// decrypt. Drains from recvBuf, which was filled by tlsRawReadCallback just
// before the enclosing mbedtls_ssl_* call.
static int sslRecvCallback(void* ctx, unsigned char* buf, size_t len) {
    TLSHandle* tls = ctx;
    size_t avail = tls->recvTail - tls->recvHead;
    if(avail == 0) return MBEDTLS_ERR_SSL_WANT_READ;
    size_t n = len < avail ? len : avail;
    memcpy(buf, tls->recvBuf + tls->recvHead, n);
    tls->recvHead += n;
    return (int)n;
}

// After each SSL operation, compact recvBuf so that any unconsumed ciphertext
// (beginning of the next TLS record) sits at the front, ready for the next
// libuv read to append to.
static void compactRecvBuf(TLSHandle* tls) {
    size_t remaining = tls->recvTail - tls->recvHead;
    if(remaining > 0 && tls->recvHead > 0) {
        memmove(tls->recvBuf, tls->recvBuf + tls->recvHead, remaining);
    }
    tls->recvHead = 0;
    tls->recvTail = remaining;
}

// ------------------------------------------------------------------------------
// TLS handshake driver
// ------------------------------------------------------------------------------

// Advance the mbedTLS handshake state machine. Called from tlsRawReadCallback
// while state == TLS_HANDSHAKING. On success, transitions to TLS_CONNECTED
// and stops libuv reads (the user starts them again via TLS_readStart).
static void tlsDriveHandshake(TLSHandle* tls) {
    uv_handle_t* handle = (uv_handle_t*)tls;

    int ret = mbedtls_ssl_handshake(&tls->ssl);
    // Flush any handshake messages queued in sendStaging (e.g. ServerHello,
    // ClientHello, Finished) regardless of the return value.
    tlsFlushSend(tls, true, -1);
    compactRecvBuf(tls);

    if(ret == 0) {
        // Handshake complete
        tls->state = TLS_CONNECTED;
        uv_read_stop((uv_stream_t*)&tls->tcp);
        reqCallback(handle, tls->handshakeCbId, true, -1, 0);
        tls->handshakeCbId = -1;
    } else if(ret == MBEDTLS_ERR_SSL_WANT_READ) {
        // Need more ciphertext - wait for the next tlsRawReadCallback invocation
    } else {
        // Fatal handshake error.
        tls->lastTlsErr = ret;
        uv_read_stop((uv_stream_t*)&tls->tcp);
        reqCallback(handle, tls->handshakeCbId, true, -1, UV_EPROTO);
        tls->handshakeCbId = -1;
    }
}

// ------------------------------------------------------------------------------
// Shared data-phase decrypt loop
// ------------------------------------------------------------------------------

// Decrypt all complete TLS records currently sitting in recvBuf and deliver
// their plaintext to the J* READ_CB. Stops when:
//   * recvBuf is empty (no more ciphertext to process)
//   * READ_CB becomes -1 (user called readStop from within a callback)
//   * mbedTLS returns WANT_READ (the current record is not yet complete)
//   * a fatal decryption error occurs
//
// This function may be called re-entrantly from TLS_readStart (a J* native)
// as well as from tlsRawReadCallback (a libuv callback). Both paths are safe
// because the J* VM supports re-entrant C->J*->C call chains and the J*
// value stack is balanced inside deliverRead.
//
// KEY INVARIANT: when tlsDrainRecvBuf returns with recvTail > recvHead,
// the remaining ciphertext belongs to a TLS record that cannot be fully
// decrypted yet (WANT_READ) - either because recvBuf holds only a prefix
// of the next wire record, or because READ_CB was revoked and will be
// re-registered by the next readStart call, which must call this function
// again to avoid a deadlock.
static void tlsDrainRecvBuf(TLSHandle* tls) {
    uv_handle_t* handle = (uv_handle_t*)tls;
    HandleMetadata* handleMetadata = handle->data;

    unsigned char plain[TLS_RECV_BUF_SIZE];
    bool retry = false;

    for(;;) {
        if(!retry && tls->recvTail == tls->recvHead) break;
        retry = false;

        // Re-check READ_CB after every delivered record. The J* callback fired
        // by deliverRead may have called readStop(), causing the read callback
        // to be stale on the handle.
        if(handleMetadata->callbacks[READ_CB] == -1) break;

        int ret = mbedtls_ssl_read(&tls->ssl, plain, sizeof(plain));
        compactRecvBuf(tls);

        // Unconditionally flush any pending messages. This ensures protocol
        // responses reach the peer promptly and keeps sendStaging empty before
        // the next ssl_read call.
        tlsFlushSend(tls, true, -1);

        if(ret > 0) {
            deliverRead(handle, plain, ret);
        } else if(ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            // Peer sent TLS close_notify - signal EOF (null data, status 0)
            if(handleMetadata->callbacks[READ_CB] != -1) {
                deliverRead(handle, NULL, 0);
            }
            break;
        } else if(ret == MBEDTLS_ERR_SSL_WANT_READ) {
            // No complete record available yet. If recvBuf still has bytes,
            // loop - the guard at the top re-enters ssl_read automatically.
            // Otherwise we genuinely need more ciphertext from the network.
            if(tls->recvTail > tls->recvHead) continue;
            else break;
        } else if(ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            // sendStaging filled during ssl_read; now flushed above. mbedTLS
            // was mid-operation and must be called again to complete it.
            retry = true;
            continue;
        } else if(ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
            // TLS 1.3 NewSessionTicket processed internally by mbedTLS.
            continue;
        } else {
            // Fatal decryption error
            tls->lastTlsErr = ret;
            if(handleMetadata->callbacks[READ_CB] != -1) {
                deliverRead(handle, NULL, UV_EPROTO);
            }
            break;
        }
    }
}

// ------------------------------------------------------------------------------
// Shared libuv read callback (handshake and data phases)
// ------------------------------------------------------------------------------

// Single read callback used from TCP-connect through the end of the connection.
// Dispatches by tls->state:
//
//   TLS_HANDSHAKING -> feeds the mbedTLS handshake state machine
//   TLS_CONNECTED   -> decrypts and delivers plaintext to the J* READ_CB
//
// buf->base always points to loopMeta->readBuf (the per-loop staging area
// provided by allocCallback), which is safe to read until this function returns.
static void tlsRawReadCallback(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    TLSHandle* tls = (TLSHandle*)stream;
    uv_handle_t* handle = (uv_handle_t*)stream;
    HandleMetadata* handleMeta = handle->data;

    if(nread < 0) {
        uv_read_stop(stream);
        switch(tls->state) {
        case TLS_HANDSHAKING:
            reqCallback(handle, tls->handshakeCbId, true, -1, (int)nread);
            tls->handshakeCbId = -1;
            break;
        case TLS_CONNECTED:
            if(handleMeta->callbacks[READ_CB] != -1) {
                deliverRead(handle, NULL, (int)nread);
            }
            break;
        }
        return;
    }

    if(nread == 0) return;

    size_t avail = TLS_RECV_BUF_SIZE - tls->recvTail;
    size_t n = (size_t)nread < avail ? (size_t)nread : avail;
    memcpy(tls->recvBuf + tls->recvTail, buf->base, n);
    tls->recvTail += n;

    switch(tls->state) {
    case TLS_HANDSHAKING:
        tlsDriveHandshake(tls);
        break;
    case TLS_CONNECTED:
        tlsDrainRecvBuf(tls);
        break;
    }
}

// ------------------------------------------------------------------------------
// Intermediate TCP-connect callback - kicks off TLS handshake
// ------------------------------------------------------------------------------

static void tlsTcpConnectedCallback(uv_connect_t* req, int status) {
    TLSHandle* tls = (TLSHandle*)req->handle;
    uv_handle_t* handle = (uv_handle_t*)tls;
    free(req);

    if(status < 0) {
        reqCallback(handle, tls->handshakeCbId, true, -1, status);
        tls->handshakeCbId = -1;
        return;
    }

    // TCP is up: start raw reads so incoming server messages feed the handshake,
    // then drive the client-side handshake.
    uv_read_start((uv_stream_t*)&tls->tcp, allocCallback, tlsRawReadCallback);
    tlsDriveHandshake(tls);
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
    if(!jsrIsNull(vm, 3)) {
        JSR_CHECK(String, 3, "serverName");
    }
    if(!jsrIsNull(vm, 4)) {
        JSR_CHECK(Function, 4, "callback");
    }

    if(!Handle_checkClosing(vm, 0)) return false;

    TLSHandle* tls = (TLSHandle*)Handle_getHandle(vm, 0);
    if(!tls) return false;

    // SNI hostname must be set before the handshake begins.
    // Passing NULL skips SNI entirely (acceptable for VERIFY_NONE connections
    // or direct IP-address connects where SNI is not meaningful).
    if(!jsrIsNull(vm, 3)) {
        int ret = mbedtls_ssl_set_hostname(&tls->ssl, jsrGetString(vm, 3));
        if(ret != 0) {
            EventHorizonException_raise(vm, "Failed to set TLS hostname (SNI)");
            return false;
        }
    }

    sockaddr_union sa;
    int res = initSockaddr(jsrGetString(vm, 1), (int)jsrGetNumber(vm, 2), &sa);
    if(res < 0) {
        StatusException_raise(vm, res);
        return false;
    }

    // Register the user callback as the handshake-completion callback.
    // It is fired when the TLS handshake (started in tlsTcpConnectedCallback) finishes.
    if(!jsrIsNull(vm, 4)) {
        tls->handshakeCbId = Handle_registerCallbackWithId(vm, 4, 0);
        if(tls->handshakeCbId == -1) return false;
    }

    uv_connect_t* req = malloc(sizeof(*req));
    // We skip setting up a J* callback at the TCP level.
    // We already set-up the J* user callback above for when the TLS handshake is done.
    setRequestCallback((uv_req_t*)req, -1);

    res = uv_tcp_connect(req, &tls->tcp, &sa.sa, tlsTcpConnectedCallback);
    if(res < 0) {
        free(req);
        if(tls->handshakeCbId != -1) {
            int callbackId = tls->handshakeCbId;
            tls->handshakeCbId = -1;
            if(!Handle_unregisterCallbackById(vm, callbackId, 0)) return false;
        }
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

// Start a server-side TLS handshake on an already-accepted TCP connection.
// Starts libuv reads so the incoming ClientHello can feed the mbedTLS state
// machine. Fires `callback(status)` when the handshake completes or fails.
bool TLS_startServerHandshake(JStarVM* vm) {
    JSR_CHECK(Function, 1, "callback");

    TLSHandle* tls = (TLSHandle*)Handle_getHandle(vm, 0);
    if(!tls) return false;

    tls->handshakeCbId = Handle_registerCallbackWithId(vm, 1, 0);
    if(tls->handshakeCbId == -1) return false;

    uv_read_start((uv_stream_t*)&tls->tcp, allocCallback, tlsRawReadCallback);
    tlsDriveHandshake(tls);

    jsrPushNull(vm);
    return true;
}

bool TLS_readStart(JStarVM* vm) {
    JSR_CHECK(Function, 1, "callback");

    if(!Handle_checkClosing(vm, 0)) return false;

    TLSHandle* tls = (TLSHandle*)Handle_getHandle(vm, 0);
    if(!tls) return false;

    if(!Handle_registerCallback(vm, 1, READ_CB, 0)) return false;

    int res = uv_read_start((uv_stream_t*)&tls->tcp, allocCallback, tlsRawReadCallback);
    if(res < 0) {
        if(res != UV_EINVAL && !Handle_unregisterCallback(vm, READ_CB, 0)) return false;
        StatusException_raise(vm, res);
        return false;
    }

    // Drain any ciphertext that arrived (and was buffered in recvBuf) during
    // a previous read cycle that broke early because READ_CB was revoked
    // mid-loop. Without this, data from TLS records that were received in the
    // same TCP segment as the record that triggered readStop would sit in
    // recvBuf forever, no new libuv read callback fires unless the peer
    // sends more bytes, causing the next readLine/readUntil to deadlock.
    //
    // This call may re-entrantly invoke the J* callback registered above.
    // That is safe: the J* VM supports re-entrant C->J*->C call chains, and
    // deliverRead balances the stack correctly.
    if(tls->recvTail > tls->recvHead && tls->state == TLS_CONNECTED) {
        tlsDrainRecvBuf(tls);
    }

    jsrPushNull(vm);
    return true;
}

bool TLS_readStop(JStarVM* vm) {
    TLSHandle* tls = (TLSHandle*)Handle_getHandle(vm, 0);
    if(!tls) return false;

    if(!Handle_unregisterCallback(vm, READ_CB, 0)) return false;
    uv_read_stop((uv_stream_t*)tls);

    jsrPushNull(vm);
    return true;
}

bool TLS_rawWrite(JStarVM* vm) {
    JSR_CHECK(String, 1, "data");
    if(!jsrIsNull(vm, 2)) {
        JSR_CHECK(Function, 2, "callback");
    }

    if(!Handle_checkClosing(vm, 0)) return false;

    TLSHandle* tls = (TLSHandle*)Handle_getHandle(vm, 0);
    if(!tls) return false;

    if(tls->state != TLS_CONNECTED) {
        EventHorizonException_raise(vm, "TLS handshake not complete");
        return false;
    }

    int cbId = -1;
    if(!jsrIsNull(vm, 2)) {
        cbId = Handle_registerCallbackWithId(vm, 2, 0);
        if(cbId == -1) return false;
    }

    // mbedtls_ssl_write calls sslSendCallback synchronously, accumulating ciphertext
    // in sendStaging. The J* string is fully consumed here (no need to keep it alive via
    // Handle_pushPending like in Streams or UDP, we pass a malloc'd copy to uv_write).
    const char* data = jsrGetString(vm, 1);
    size_t len = jsrGetStringSz(vm, 1);
    size_t written = 0;

    while(written < len) {
        int ret = mbedtls_ssl_write(&tls->ssl, (const unsigned char*)data + written, len - written);
        if(ret > 0) {
            written += (size_t)ret;
        } else if(ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            // sendStaging is full - flush the partial ciphertext first, then retry
            tlsFlushSend(tls, true, -1);
        } else {
            if(!Handle_unregisterCallbackById(vm, cbId, 0)) return false;
            TLSException_raise(vm, ret);
            return false;
        }
    }

    // Flush the final (or only) batch of ciphertext.
    tlsFlushSend(tls, cbId == -1, cbId);

    jsrPushNull(vm);
    return true;
}

bool TLS_tlsError(JStarVM* vm) {
    TLSHandle* tls = (TLSHandle*)Handle_getHandle(vm, 0);
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
    int cbId = getRequestCallback((uv_req_t*)req);
    uv_handle_t* handle = (uv_handle_t*)req->handle;
    free(req);
    reqCallback(handle, cbId, true, -1, status);
}

bool TLS_shutdown(JStarVM* vm) {
    if(!jsrIsNull(vm, 1)) {
        JSR_CHECK(Function, 1, "callback");
    }

    if(!Handle_checkClosing(vm, 0)) return false;

    TLSHandle* tls = (TLSHandle*)Handle_getHandle(vm, 0);
    if(!tls) return false;

    // Send TLS close_notify alert before closing the TCP stream
    mbedtls_ssl_close_notify(&tls->ssl);
    tlsFlushSend(tls, true, -1);

    uv_shutdown_t* req = malloc(sizeof(*req));

    int cbId = -1;
    if(!jsrIsNull(vm, 1)) {
        cbId = Handle_registerCallbackWithId(vm, 1, 0);
        if(cbId == -1) {
            free(req);
            return false;
        }
    }

    setRequestCallback((uv_req_t*)req, cbId);

    int res = uv_shutdown(req, (uv_stream_t*)&tls->tcp, tlsShutdownCallback);
    if(res < 0) {
        free(req);
        if(!Handle_unregisterCallbackById(vm, cbId, 0)) return false;
        StatusException_raise(vm, res);
        return false;
    }

    jsrPushNull(vm);
    return true;
}

bool TLS_close(JStarVM* vm) {
    if(!jsrIsNull(vm, 1)) {
        JSR_CHECK(Function, 1, "callback");
    }

    if(!Handle_checkClosing(vm, 0)) return false;

    TLSHandle* tls = (TLSHandle*)Handle_getHandle(vm, 0);
    if(!tls) return false;

    if(!jsrIsNull(vm, 1) && !Handle_registerCallback(vm, 1, CLOSE_CB, 0)) {
        return false;
    }

    // If a TLS session is active, send close_notify best-effort before releasing
    // the handle. We use uv_try_write (synchronous, non-blocking) rather than an
    // async uv_write + uv_shutdown so that close() remains non-blocking and its
    // callback fires as soon as uv_close fires, not after a network round-trip.
    //
    // close_notify is a small record (~30 bytes) and the kernel send buffer is
    // almost always able to absorb it immediately. If uv_try_write fails (broken
    // connection, write queue already occupied, etc.) we silently proceed.
    if(tls->state == TLS_CONNECTED) {
        mbedtls_ssl_close_notify(&tls->ssl);
        if(tls->sendStagingLen > 0) {
            uv_buf_t buf = uv_buf_init((char*)tls->sendStaging, tls->sendStagingLen);
            uv_try_write((uv_stream_t*)&tls->tcp, &buf, 1);
            tls->sendStagingLen = 0;
        }
    }

    uv_close((uv_handle_t*)tls, &closeCallback);

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
    TLSHandle* tls = pushTLSHandle(vm);

    mbedtls_ssl_init(&tls->ssl);
    mbedtls_ssl_config_init(&tls->conf);
    mbedtls_x509_crt_init(&tls->caCert);
    mbedtls_x509_crt_init(&tls->ownCert);
    mbedtls_pk_init(&tls->pk);

#if MBEDTLS_VERSION_NUMBER < 0x04000000
    mbedtls_ctr_drbg_init(&tls->ctrDrbg);
    mbedtls_entropy_init(&tls->entropy);
#endif

    tls->state = TLS_HANDSHAKING;
    tls->handshakeCbId = -1;
    tls->lastTlsErr = 0;
    tls->recvHead = 0;
    tls->recvTail = 0;
    tls->sendStagingLen = 0;

    uv_loop_t* loop = EventLoop_getUVLoop(vm, 1);
    if(!loop) return false;
    uv_tcp_init(loop, &tls->tcp);

#if MBEDTLS_VERSION_NUMBER < 0x04000000
    int rngRet = mbedtls_ctr_drbg_seed(&tls->ctrDrbg, mbedtls_entropy_func, &tls->entropy, NULL, 0);
    if(rngRet != 0) {
        TLSException_raise(vm, rngRet);
        return false;
    }
#else
    // mbedTLS 4.x delegates all RNG to PSA Crypto; seed it once here.
    psa_status_t psaRet = psa_crypto_init();
    if(psaRet != PSA_SUCCESS) {
        TLSException_raise(vm, MBEDTLS_ERR_SSL_INTERNAL_ERROR);
        return false;
    }
#endif

    bool isServer = jsrGetBoolean(vm, 5);
    int endpoint = isServer ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT;

    int ret = mbedtls_ssl_config_defaults(&tls->conf, endpoint, MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT);
    if(ret != 0) {
        TLSException_raise(vm, ret);
        return false;
    }

#if MBEDTLS_VERSION_NUMBER < 0x04000000
    // mbedTLS 4.x removed ssl_conf_rng; PSA Crypto manages the RNG after
    // psa_crypto_init() and ssl_config_defaults wires it up automatically.
    mbedtls_ssl_conf_rng(&tls->conf, mbedtls_ctr_drbg_random, &tls->ctrDrbg);
#endif

    // Load the CA certificate for peer verification (optional).
    // Without a CA, peer verification is disabled.
    //
    // TODO: Should we try to automatically add system CA bundles in here?
    // TODO: Should we warn about disabled cert verification?
    //       At least we should warn if we can't find system CA bundeles...
    if(!jsrIsNull(vm, 2)) {
        ret = mbedtls_x509_crt_parse_file(&tls->caCert, jsrGetString(vm, 2));
        if(ret != 0) {
            TLSException_raise(vm, ret);
            return false;
        }
        mbedtls_ssl_conf_ca_chain(&tls->conf, &tls->caCert, NULL);
        mbedtls_ssl_conf_authmode(&tls->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    } else {
        mbedtls_ssl_conf_authmode(&tls->conf, MBEDTLS_SSL_VERIFY_NONE);
    }

    // Load own certificate and private key (required for servers; optional for clients doing mutual
    // TLS authentication).
    if(!jsrIsNull(vm, 3) && !jsrIsNull(vm, 4)) {
        ret = mbedtls_x509_crt_parse_file(&tls->ownCert, jsrGetString(vm, 3));
        if(ret != 0) {
            TLSException_raise(vm, ret);
            return false;
        }
#if MBEDTLS_VERSION_NUMBER < 0x04000000
        ret = mbedtls_pk_parse_keyfile(&tls->pk, jsrGetString(vm, 4), NULL, mbedtls_ctr_drbg_random,
                                       &tls->ctrDrbg);
#else
        // In 4.x the f_rng/p_rng parameters were removed; PSA Crypto handles
        // key decryption RNG internally.
        ret = mbedtls_pk_parse_keyfile(&tls->pk, jsrGetString(vm, 4), NULL);
#endif
        if(ret != 0) {
            TLSException_raise(vm, ret);
            return false;
        }
        ret = mbedtls_ssl_conf_own_cert(&tls->conf, &tls->ownCert, &tls->pk);
        if(ret != 0) {
            TLSException_raise(vm, ret);
            return false;
        }
    }

    ret = mbedtls_ssl_setup(&tls->ssl, &tls->conf);
    if(ret != 0) {
        TLSException_raise(vm, ret);
        return false;
    }

    // Wire up the memory BIOs
    mbedtls_ssl_set_bio(&tls->ssl, tls, sslSendCallback, sslRecvCallback, NULL);

    return true;
}
