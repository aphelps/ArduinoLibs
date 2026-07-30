# RS485Utils

## Purpose
A `Socket` transport over an RS485 two-wire bus, giving addressed message passing on a
multi-drop serial bus. See [../Socket/](../Socket/) for the shared transport API.

## Key API
- `class RS485Socket : public Socket`
  - `RS485Socket(SERIAL_TYPE *serial, byte enablePin, socket_addr_t address)` — use an
    existing serial port and a driver-enable (DE/RE) pin.
  - `RS485Socket(byte recvPin, byte xmitPin, byte enablePin, socket_addr_t address[, boolean debug])`
    — construct a `SoftwareSerial` internally.
  - `void init(...)` overloads mirror the constructors, adding `recvsize` and `debug`.
  - Overrides the `Socket` API: `setup()`, `initBuffer()`, `sendMsgTo()`, `getMsg()`,
    `getLength()`, `headerFromData()`, `sourceFromData()`, `destFromData()`, `initialized()`.
- Helpers: `void printSocketMsg(const rs485_socket_msg_t *msg)`,
  `void printBuffer(const byte *buff, int length)`.
- On-wire header `rs485_socket_hdr_t` — **7 bytes on every target**, `__attribute__((__packed__))`
  with `static_assert`s pinning the size and all five field offsets
  (`ID@0 length@1 source@2 address@4 flags@6`). Size a send buffer with `RS485_BUFFER_TOTAL(n)`.
  (The `*_FROM_DATA` macros are marked deprecated.)
  The packing is load-bearing, not cosmetic: `sizeof(rs485_socket_hdr_t)` is what places the payload
  on both send and receive, and unpacked it is 7 on AVR but 8 on any 2-byte-aligned ABI — so an ESP32
  and an ATMega328 would disagree about where the payload starts and misparse every frame between
  them. See the comment on the struct for the full story.

## Configuration
- `RS485_HARDWARE_SERIAL` — the one real compile toggle: when defined (e.g.
  `-DRS485_HARDWARE_SERIAL=Serial1`) the serial type is `HardwareSerial`; otherwise it defaults
  to `SoftwareSerial`. Use hardware serial (`Serial1`/`Serial2`) on ESP32.
- `RS485_RECV_BUFFER` — receive buffer size; a plain (unguarded) `#define` in `RS485Utils.h`
  (64), so change it by editing the header, not via `-D`.
- `DEFAULT_BAUD` (28000) — a `static const` member of `RS485Socket`, not a compile flag; the
  baud is passed to `setup()` at runtime.

## Dependencies
- Sibling: `Socket` (base class).
- External: Nick Gammon's `RS485_non_blocking`.

## Example
[../platformio/RS485SocketTool/](../platformio/RS485SocketTool/) (also
`examples/RS485SocketTool/`).

---
Part of [ArduinoLibs](../README.md).
