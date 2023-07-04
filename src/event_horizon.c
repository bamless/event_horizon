#include "event_horizon.h"

#include "consts.h"
#include "dns.h"
#include "errors.h"
#include "event_loop.h"
#include "handle.h"
#include "idle.h"
#include "pipe.h"
#include "stream.h"
#include "tcp.h"
#include "timer.h"
#include "udp.h"

inline void setRequestCallback(uv_req_t* req, int callbackId);
inline int getRequestCallback(uv_req_t* req);

// clang-format off
static JStarNativeReg registry[] = {
    JSR_REGFUNC(_consts_init, consts_init)
    JSR_REGFUNC(getAddrInfo, dns_getAddrInfo)
    JSR_REGFUNC(strerror, errors_strerror)

    JSR_REGMETH(EventLoop, run, EventLoop_run)
    JSR_REGMETH(EventLoop, stop, EventLoop_stop)
    JSR_REGMETH(EventLoop, alive, EventLoop_alive)
    JSR_REGMETH(EventLoop, walk, EventLoop_walk)
    JSR_REGMETH(EventLoop, _init, EventLoop_init)

    JSR_REGMETH(Handle, close, Handle_close)
    JSR_REGMETH(Handle, isActive, Handle_isActive)
    JSR_REGMETH(Handle, isClosing, Handle_isClosing)
    JSR_REGMETH(Handle, _init, Handle_init)
    JSR_REGMETH(Handle, sendBufferSize, Handle_sendBufferSize)
    JSR_REGMETH(Handle, setSendBufferSize, Handle_setSendBufferSize)
    JSR_REGMETH(Handle, recvBufferSize, Handle_recvBufferSize)
    JSR_REGMETH(Handle, setRecvBufferSize, Handle_setRecvBufferSize)
    JSR_REGMETH(Handle, fileno, Handle_fileno)

    JSR_REGMETH(Stream, write, Stream_write)
    JSR_REGMETH(Stream, tryWrite, Stream_tryWrite)
    JSR_REGMETH(Stream, readStart, Stream_readStart)
    JSR_REGMETH(Stream, readStop, Stream_readStop)
    JSR_REGMETH(Stream, shutdown, Stream_shutdown)
    JSR_REGMETH(Stream, isReadable, Stream_isReadable)
    JSR_REGMETH(Stream, isWritable, Stream_isWritable)
    JSR_REGMETH(Stream, getWriteQueueSize, Stream_getWriteQueueSize)
    JSR_REGMETH(Stream, _rawListen, Stream_rawListen)
    JSR_REGMETH(Stream, _rawAccept, Stream_rawAccept)

    JSR_REGMETH(TCP, connect, TCP_connect)
    JSR_REGMETH(TCP, bind, TCP_bind)
    JSR_REGMETH(TCP, sockName, TCP_sockName)
    JSR_REGMETH(TCP, peerName, TCP_peerName)

    JSR_REGMETH(UDP, bind, UDP_bind)
    JSR_REGMETH(UDP, connect, UDP_connect)
    JSR_REGMETH(UDP, send, UDP_send)
    JSR_REGMETH(UDP, trySend, UDP_trySend)
    JSR_REGMETH(UDP, recvStart, UDP_recvStart)
    JSR_REGMETH(UDP, recvStop, UDP_recvStop)
    JSR_REGMETH(UDP, sockName, UDP_sockName)
    JSR_REGMETH(UDP, peerName, UDP_peerName)
    JSR_REGMETH(UDP, sendQueueSize, UDP_sendQueueSize)
    JSR_REGMETH(UDP, sendQueueCount, UDP_sendQueueCount)
    JSR_REGMETH(UDP, setMembership, UDP_setMembership)
    JSR_REGMETH(UDP, setSourceMembership, UDP_setSourceMembership)
    JSR_REGMETH(UDP, setMulticastLoop, UDP_setMulticastLoop)
    JSR_REGMETH(UDP, setMulticastTTL, UDP_setMulticastTTL)
    JSR_REGMETH(UDP, setMulticastInterface, UDP_setMulticastInterface)
    JSR_REGMETH(UDP, setBroadcast, UDP_setBroadcast)

    JSR_REGMETH(Idle, start, Idle_start)
    JSR_REGMETH(Idle, stop, Idle_stop)

    JSR_REGMETH(Timer, start, Timer_start)
    JSR_REGMETH(Timer, stop, Timer_stop)
    JSR_REGMETH(Timer, again, Timer_again)
    JSR_REGMETH(Timer, setRepeat, Timer_setRepeat)
    JSR_REGMETH(Timer, repeat, Timer_repeat)
    JSR_REGMETH(Timer, dueIn, Timer_dueIn)

    JSR_REGMETH(Pipe, open, Pipe_open)
    JSR_REGMETH(Pipe, bind, Pipe_bind)
    JSR_REGMETH(Pipe, connect, Pipe_connect)
    JSR_REGMETH(Pipe, sockName, Pipe_sockName)
    JSR_REGMETH(Pipe, peerName, Pipe_peerName)

    JSR_REGEND
};

JSTAR_API JStarNativeReg* jsrOpenModule(void) {
    return registry;
}
