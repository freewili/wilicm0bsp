# Architecture

```text
Python app -> onewili_cm0 -> fwcm0 api --+
                                      +-> fwcm0-bridge -> FPGA mailbox -> MAIN
C++ app -> wilicm0::Device (socket) ----+
```

The public OneWili submodule supplies generated command builders and response
decoders. The BSP supplies a C++ RAII socket transport; the driver supplies
the hardware link and daemon. The API session probe reads Device State before
returning a usable connection. Missing sockets, busy sessions and failed
handshakes are errors. C++ methods return `ow_status`; Python methods return
`Result`. Neither adapter automatically retries a timed-out hardware write.

The bridge's socket request is a little-endian length-prefixed `OP_API` (7),
followed by a raw duplex command/reply stream. Half-closing input releases the
session; C++ waits up to one second for the bridge to finish before closing.
Use one application thread at a time for a Device instance.

The mailbox allows 511 command text bytes and 4,095 captured reply bytes.
File transfers use bounded framed menu operations with CRC/size checks.
Python `device.files.put/get/put_file/get_file` use those operations on CM0.
C++ can include `onewili_framed_files.h` from the linked OneWili C package for the
same protocol. Only one file-transfer session is supported at a time.

Binary streaming decoders are available in upstream OneWili, but there is no
FTDI-to-mailbox forwarding route. Logic analyzer event buffers and CAN FD
binary events require the separate host USB binary connection. For CM0 CAN
receive, use OneWili's enable/receive queue commands instead of streaming.
