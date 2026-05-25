#ifndef TLS_H
#define TLS_H

#include <mbedtls/build_info.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#if MBEDTLS_VERSION_NUMBER < 0x04000000
    #include <mbedtls/ctr_drbg.h>
    #include <mbedtls/entropy.h>
#else
    #include <psa/crypto.h>
#endif
#include <jstar/jstar.h>
#include <uv.h>

// Staging buffer capacity. Sized to match LOOP_READ_BUF_SIZE so a single
// libuv read can always be fully absorbed in one shot.
#define TLS_RECV_BUF_SIZE     65536
#define TLS_SEND_STAGING_SIZE 65536

typedef enum TLSState {
    TLS_HANDSHAKING,
    TLS_CONNECTED,
} TLSState;

// TLSHandle wraps a uv_tcp_t and a per-connection mbedTLS context.
typedef struct TLSHandle {
    uv_tcp_t tcp;

    // mbedTLS contexts - all initialised in uvTLS(), freed in freeTLSHandle()
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

    // Callback ID (from Handle_registerCallbackWithId) fired when the TLS
    // handshake completes or fails. -1 when no handshake is in progress.
    int handshakeCbId;

    // Last mbedTLS error code from a failed handshake or decryption step.
    // Set before firing the J* callback so the J* side can retrieve the
    // detailed error string via TLS_tlsError() / _tlsError(). Reset to 0
    // by TLS_tlsError after it is consumed.
    int lastTlsErr;

    // Receive staging: raw ciphertext written by tlsRawReadCallback and
    // drained by sslRecvCallback (called from within mbedtls_ssl_* functions).
    unsigned char recvBuf[TLS_RECV_BUF_SIZE];
    size_t recvHead;
    size_t recvTail;

    // Send staging: ciphertext accumulated by sslSendCallback during an SSL
    // operation and flushed to libuv via a single uv_write afterwards.
    unsigned char sendStaging[TLS_SEND_STAGING_SIZE];
    size_t sendStagingLen;
} TLSHandle;

// class TLS is Stream
bool TLS_bind(JStarVM* vm);
bool TLS_connect(JStarVM* vm);
bool TLS_tlsError(JStarVM* vm);
bool TLS_sockName(JStarVM* vm);
bool TLS_peerName(JStarVM* vm);
bool TLS_readStart(JStarVM* vm);
bool TLS_readStop(JStarVM* vm);
bool TLS_rawWrite(JStarVM* vm);
bool TLS_shutdown(JStarVM* vm);
bool TLS_close(JStarVM* vm);
bool TLS_startServerHandshake(JStarVM* vm);
// end

bool uvTLS(JStarVM* vm);

#endif
