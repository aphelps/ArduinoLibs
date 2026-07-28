# XBeeSocket

## Purpose
A `Socket` transport over an XBee / ZigBee radio, giving addressed message passing between
XBee nodes. See [../Socket/](../Socket/) for the shared transport API.

## Key API
- `class XBeeSocket : public Socket`
  - `XBeeSocket(XBee *xbee, socket_addr_t address)` and the matching `void init(XBee *xbee, socket_addr_t address)`
    — wraps a caller-supplied `XBee*` (and internally a `ZBRxResponse`).
  - Overrides the `Socket` API: `setup()`, `initialized()`, `initBuffer()`, `sendMsgTo()`,
    `getMsg()`, `getLength()`, `headerFromData()`, `sourceFromData()`, `destFromData()`.
- On-wire header `xbee_socket_hdr_t`; buffer-sizing macros `XBEE_BUFFER_TOTAL(n)` /
  `XBEE_DATA_LENGTH(n)`.

## Configuration
None via `-D` flags — the radio object and address are supplied through the constructor /
`init()`.

## Dependencies
- Sibling: `Socket` (base class).
- External: `XBee`.

## Example
[../platformio/XBeeSocketTest/](../platformio/XBeeSocketTest/).

---
Part of [ArduinoLibs](../README.md).
