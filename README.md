# Event Horizon: Asynchronous I/O for the [J*](https://github.com/bamless/jstar) language


Event Horizon extends [J*](https://github.com/bamless/jstar) with asynchronous I/O, letting you write concurrent network and I/O code
using an `async/await`-like syntax. Under the hood, the library drives a single-threaded
[libuv](https://libuv.org) event loop that multiplexes I/O readiness events and schedules callbacks
when data is available.

## Features

- **`async/await`**: coroutine-based concurrency via the `@async` decorator and `yield`
- **Futures and Tasks**: explicit result containers and scheduled coroutines
- **TCP**: `TCPStream` for clients and servers, with automatic DNS resolution
- **TLS**: `TLSStream` for encrypted TCP connections (client and server), backed by [mbedTLS](https://github.com/Mbed-TLS/mbedtls)
- **UDP**: `UDPSocket`, including multicast support
- **Pipes**: `PipeStream` for cross-platform pipes (Unix domain sockets on Unix, named pipes on Windows)
- **Timers**: `sleep()` for task-friendly delays
- **DNS**: asynchronous `getAddrInfo()`
- **Raw libuv bindings**: `event_horizon.uv` for callback-style code

## How it works

Event Horizon uses J*'s generator protocol to simulate `async/await`. Marking a function with the
`@async` decorator causes it to return a lazy coroutine object. Calling the function does not start
execution - scheduling it as a `Task` does. A coroutine is scheduled when it is passed to `evh.run()`
or `loop.runUntilComplete()`, passed to `evh.createTask()` or `loop.createTask()`, yielded (awaited)
from a running task, or passed as an input to combinators such as `all()`, `race()`, and `waitAll()`.

Inside an async function, `yield`ing a `Future`, `Task`, or coroutine suspends the current task until
the awaitable completes. If it completes with a value, execution resumes with that value; if it
completes with an exception, the exception is re-raised and can be caught with `try/except`.

The event loop is started explicitly by passing the top-level coroutine, `Future`, or `Task` to
`evh.run()`, which blocks until that awaitable has completed.

For code that needs explicit loop control, `getEventLoop()` returns the currently running loop, or the
default loop when no loop is running. `getRunningEventLoop()` returns the loop currently executing
callbacks, or `null` outside a running loop. `getDefaultEventLoop()` always returns the process-wide
default loop.

Futures, Tasks, and handles are owned by exactly one event loop. High-level operations reuse the
loop attached to their handle, while coroutine objects stay loop-neutral until they are scheduled as
Tasks. Awaiting a Future from a different loop raises an error instead of transferring it implicitly.

### Futures and Tasks

A `Future` is an explicit placeholder for a value that will be available later. It starts pending,
then completes once with either a result, an exception, or a cancellation. Code that exposes callback
or handle-based I/O usually creates a Future with `loop.createFuture()`, completes it with
`setResult()` or `setException()`, and returns it to async callers. Awaiting a failed or cancelled
Future raises the stored exception in the awaiting task.

A `Task` is a Future that runs a coroutine. Creating a Task schedules the coroutine on its owning
event loop; the Task completes when the coroutine returns, raises, or is cancelled. Use
`evh.createTask()` from async code for explicit background work, or `loop.createTask()` when you are
already threading a specific loop through lower-level code.

Cancelling a Future marks it as cancelled and causes future `result()` calls or awaits to raise
`CancelledError`. Cancelling a Task requests cancellation by throwing `CancelledError` into the
running coroutine on its next step. The coroutine can use `ensure` blocks for cleanup before the Task
finishes as cancelled, or catch the cancellation and return a value if it intentionally handles it.

### Combining awaitables

Event Horizon exposes a small set of focused combinators for working with compound awaitables:

| Function | Description |
|---|---|
| `all(...awaitables)` | Complete with ordered results when every awaitable succeeds. If one raises or is cancelled, cancel unfinished siblings and raise that error. |
| `race(...awaitables)` | Complete with the first awaitable to return a value or raise an exception. A cancelled awaitable withdraws from the race; if every awaitable is cancelled, the race raises `CancelledError`. Unfinished siblings are cancelled after a winner is found. |
| `waitAll(...awaitables)` | Wait until every awaitable finishes and return ordered `WaitOutcome` records. Child errors and cancellations are reported as outcomes instead of raising from the returned Future. |

`WaitOutcome` has public `state` and `value` fields. `state` is one of
`WaitOutcomeState.COMPLETED`, `WaitOutcomeState.FAILED`, or `WaitOutcomeState.CANCELLED`.
`value` is the returned value for fulfilled outcomes and the exception for rejected or cancelled
outcomes.

Generic event loop interface:

| Method | Description |
|---|---|
| `runUntilComplete(awaitable)` | Block until a Future, Task, or coroutine completes, then return its result or raise its exception. |
| `runForever()` | Run callbacks until `stop()` is called or the loop has no active handles left. |
| `stop()` | Ask the loop to stop on the next suitable turn. |
| `close()` | Close the loop; raises if there are still pending handles. |
| `isRunning()` | Return whether this loop is currently running callbacks. |
| `isClosed()` | Return whether this loop has been closed. |
| `callSoon(callback)` | Schedule `callback` for the next loop turn and return a cancel function. |
| `callLater(ms, callback)` | Schedule `callback` after `ms` milliseconds and return a cancel function. |
| `createFuture()` | Create a pending Future owned by this loop. |
| `createTask(coro)` | Schedule a coroutine on this loop and return a Task. |
| `sleep(ms)` | Return a Future that completes after `ms` milliseconds. |

## Module reference

| Module | Exports |
|---|---|
| `event_horizon` | `run`, `all`, `race`, `waitAll`, `WaitOutcome`, `WaitOutcomeState`, `sleep`, `createTask`, `ensureFuture`, `getEventLoop`, `getRunningEventLoop`, `getDefaultEventLoop` |
| `event_horizon.async` | `async`, `Coroutine` |
| `event_horizon.future` | `Future`, `ensureFuture` |
| `event_horizon.task` | `Task` |
| `event_horizon.tcp` | `TCPStream` |
| `event_horizon.tls` | `TLSStream` |
| `event_horizon.udp` | `UDPSocket` |
| `event_horizon.pipe` | `PipeStream` |
| `event_horizon.dns` | `getAddrInfo` |
| `event_horizon.uv` | Raw libuv bindings |

## Example

The following snippet implements a TCP echo server. Error handling is omitted for brevity; see the
`examples/` folder for complete, production-style examples.

```jstar
import event_horizon as evh
import event_horizon.async for async
import event_horizon.tcp for TCPStream

// The `@async` decorator turns this function into a lazy coroutine.
// Inside it, `yield` suspends execution until the awaited Future completes.
@async fun handleClient(client)
    var data
    while data = yield client.readLine()
        yield client.write(data)
    end
end

@async fun main()
    var server = TCPStream()
    server.bind("0.0.0.0", 8080)
    // `listen` returns a Future that completes when the server stops accepting connections.
    yield server.listen(handleClient)
end

// Start the event loop and run the top-level coroutine to completion.
evh.run(main())
```

## Raw libuv bindings

While `async/await` is the default and preferred way to write asynchronous code with Event Horizon,
the `event_horizon.uv` module exposes thin, callback-based wrappers around libuv for cases where
direct control over the event loop is needed.

`event_horizon.uv.loop()` returns the default raw loop object. It implements the same generic loop
interface described above, plus internal libuv-oriented helpers used by the raw wrappers and tests:
`_alive()` reports whether libuv has active handles or requests, and `_walk(callback)` visits active
handles with `callback(handle)`. Prefer the generic methods for application code; reach for these
helpers only when writing low-level wrapper code or diagnostics.

## Installation

### Prerequisites

- [J*](https://github.com/bamless/jstar) ≥ 1.0
- [CMake](https://cmake.org) ≥ 3.10
- [libuv](https://libuv.org) ≥ 1.0 (or use the bundled build described below)
- [mbedTLS](https://github.com/Mbed-TLS/mbedtls) ≥ 3.0 (or use the bundled build described below)

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

### Bundling dependencies

Both libuv and mbedTLS can be downloaded and compiled as part of the build,
without requiring them to be installed on your system:

```bash
cmake -B build -DEVH_VENDOR_LIBUV=ON -DEVH_VENDOR_MBEDTLS=ON
cmake --build build -j
sudo cmake --install build
```

The options are independent - you can vendor either library individually if the
other is already available system-wide.

## Running the tests

```bash
jstar tests/run.jsr
```

### TLS test certificates

The TLS tests (`tests/evh/tls.jsr`, `tests/uv/tls.jsr`) require a private key that
is **not** stored in the repository. Before running the full suite for the first time,
generate a self-signed certificate and key in `tests/certs/`:

```bash
openssl req -x509 -newkey rsa:2048 \
    -keyout tests/certs/key.pem \
    -out    tests/certs/cert.pem \
    -days 3650 -nodes \
    -subj "/CN=localhost" \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,digitalSignature,keyEncipherment" \
    -addext "extendedKeyUsage=serverAuth"
```
