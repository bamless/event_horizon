# Event Horizon - Asynchronous I/O for the [J*](https://github.com/bamless/jstar) language.

Event Horizon is a library that provides asynchronous I/O functionality to the J* language for writing
concurrent code using an `async/await`-like syntax.  
Behind the scenes the library uses `libuv` to run a sungle-threaded event loop that handles listening
for file descriptor changes and scheduling callbacks when data is available.

## Examples

You can find more examples in the `examples` folder, the following is a short snippet demonstrating
how to write an echo server using the `async-await` syntax (NOTE: this code deliberately skips error
handling to make the example simpler):
```
import event_horizon as evh
import event_horizon.async for async
import event_horizon.tcp for TCPStream

@async  // This decorator enables our function to await for promises via `yield` statements
fun handleClient(client)
    var data
    // `client.readLine` and `client.write` return `Promise`s. A `Promise` represent a not-yet
    // available result, and must be `await`ed in order for its value to be resolved at some point
    // in the fututre
    while data = yield client.readLine()  // To `await` for a Promise, we `yield` it
        yield client.write(data)          // Same thing here
    end
end

@async
fun main()
    var server = TCPStream()
    server.bind("0.0.0.0", 8080)
    yield server.listen(handleClient)
end

// This is the entry point for asynchronous code. It runs the event loop untill all aynschronous
// operations are completed. If a promise is passed to it it will attach error handlers to gracefully
// handle any errors in case of the promise being rejected
evh.run(main())
```

## Libuv binding

While the `async/await` syntax is the default and preferred method to write asynchronous code using
Event Horizon, raw bindings are provided in the `event_horizon.uv` module that provide a
thin wrapper to `libuv` for writing async code with good ol' callbacks.

## How to install

Ensure you have `libuv` installed on your system. Then, simply issue this on your terminal:
```bash
mkdir build;
cd build;
cmake ..;
make -j;
sudo make install;
```

Then you can start using the library in your J* code:
```
import event_horizon as evh
```

### Withouth installing libuv

You can choose to bundle `libuv` with the library by setting the `EVH_VENDOR_LIBUV` CMake option to
`ON`. This will download and compile `libuv` as part of the build process, not requiring you to have
it installed on your system.

