# Event Horizon: Asynchronous I/O for the [J*](https://github.com/bamless/jstar) language


Event Horizon extends [J*](https://github.com/bamless/jstar) with asynchronous I/O, letting you write concurrent network and I/O code
using an `async/await`-like syntax. Under the hood, the library drives a single-threaded
[libuv](https://libuv.org) event loop that multiplexes I/O readiness events and schedules callbacks
when data is available.

## Features

- **`async/await`**: coroutine-based concurrency via the `@async` decorator and `yield`
- **Promises**: `Promise`, `Promise.all()`, `asResolved()`, `asRejected()`
- **TCP**: `TCPStream` for clients and servers, with automatic DNS resolution
- **UDP**: `UDPSocket`, including multicast support
- **Pipes**: `PipeStream` for cross-platform pipes (Unix domain sockets on Unix, named pipes on Windows)
- **Timers**: `wait()`, `setTimeout()`, `setInterval()`, `nextTick()`
- **DNS**: asynchronous `getAddrInfo()`
- **Raw libuv bindings**: `event_horizon.uv` for callback-style code

## How it works

Event Horizon uses J*'s generator protocol to simulate `async/await`. Marking a function with the
`@async` decorator causes it to return a `Promise` instead of a plain value. Inside such a
function, `yield`ing a `Promise` suspends the coroutine until that promise settles: if it
fulfills, execution resumes with the resolved value; if it rejects, the rejection is re-raised
as an ordinary exception that can be caught with `try/except`.

The event loop is started explicitly by passing the top-level `Promise` to `evh.run()`, which
blocks until all pending asynchronous operations have completed.

## Module reference

| Module | Exports |
|---|---|
| `event_horizon` | `run()` |
| `event_horizon.async` | `async` |
| `event_horizon.promise` | `Promise`, `asResolved`, `asRejected`, `all` |
| `event_horizon.tcp` | `TCPStream` |
| `event_horizon.udp` | `UDPSocket` |
| `event_horizon.pipe` | `PipeStream` |
| `event_horizon.timers` | `wait`, `waitOneTick`, `setTimeout`, `setInterval`, `nextTick` |
| `event_horizon.dns` | `getAddrInfo` |
| `event_horizon.uv` | Raw libuv bindings |

## Example

The following snippet implements a TCP echo server. Error handling is omitted for brevity; see the
`examples/` folder for complete, production-style examples.

```jstar
import event_horizon as evh
import event_horizon.async for async
import event_horizon.tcp for TCPStream

// The `@async` decorator turns this function into a coroutine that returns a Promise.
// Inside it, `yield` suspends execution until the awaited Promise settles.
@async
fun handleClient(client)
    var data
    while data = yield client.readLine()
        yield client.write(data)
    end
end

@async
fun main()
    var server = TCPStream()
    server.bind("0.0.0.0", 8080)
    // `listen` returns a Promise that resolves when the server stops accepting connections.
    yield server.listen(handleClient)
end

// Start the event loop. Passing the Promise lets evh.run() attach an error handler
// so any unhandled rejection prints a stack trace instead of silently disappearing.
evh.run(main())
```

## Raw libuv bindings

While `async/await` is the default and preferred way to write asynchronous code with Event Horizon,
the `event_horizon.uv` module exposes thin, callback-based wrappers around libuv for cases where
direct control over the event loop is needed.

## Installation

### Prerequisites

- [J*](https://github.com/bamless/jstar) ≥ 1.0
- [CMake](https://cmake.org) ≥ 3.10
- [libuv](https://libuv.org) (or use the bundled build described below)

### Building

```bash
cmake -B build
cmake --build build -j
sudo cmake --install build
```

Then import the library in your J* code:

```jstar
import event_horizon as evh
```

### Bundling libuv

Set the `EVH_VENDOR_LIBUV` CMake option to `ON` to download and compile libuv as part of the build,
without requiring it to be installed on your system:

```bash
cmake -B build -DEVH_VENDOR_LIBUV=ON
cmake --build build -j
sudo cmake --install build
```