# Event Horizon

Event horizon provides an event loop for asynchronous I/O to the **J\*** language.
It is based on `libuv` - the same event loop powering nodejs - and acts as a thin wrapper to it.

For now, the library provides almost a direct port of the `libuv` interface, making it not too ergonomic
to write asynchronous code due to callback hell. An extension adding support for promises and generators
is planned, so in the future will be possible to use this library using `async/await` syntax.  
Wheter support will be added directly in this library or in another one wrapping it is to be decided.

The library is still under development, and for now focuses on aysnchronous network I/O.

## Examples

You can find an example implementation of an echo server and echo client in the `example` folder.
