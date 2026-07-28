# RFM69Socket

## Purpose
A `Socket` transport over an RFM69 sub-GHz packet radio, giving addressed (and encrypted)
message passing between nodes. See [../Socket/](../Socket/) for the shared transport API.

## Key API
- `class RFM69Socket : public Socket`
  - `RFM69Socket(socket_addr_t address, uint8_t network_ID, uint8_t irq_pin, boolean high_power, uint8_t freq)`
    and the matching `void init(...)`.
  - `void setEncryptionKey(const char *key)` — set the AES key shared by the network.
  - Overrides the `Socket` API: `setup()`, `initialized()`, `initBuffer()`, `sendMsgTo()`,
    `getMsg()`, `getLength()`, `headerFromData()`, `sourceFromData()`, `destFromData()`.
- On-wire header `rfm69_socket_hdr_t`; buffer-sizing macros `RFM69_BUFFER_TOTAL(n)` /
  `RFM69_DATA_LENGTH(n)`. `RFM69_BROADCAST_CONVERT` maps the RFM69 broadcast address
  (`RF69_BROADCAST_ADDR`) to `SOCKET_ADDR_ANY`.

## Configuration
None via `-D` flags — the radio (address, network ID, IRQ pin, high-power flag, frequency)
and encryption key are set through the constructor / `init()` / `setEncryptionKey()`.

## Dependencies
- Sibling: `Socket` (base class).
- External: LowPowerLab `RFM69`.

## Example
[../platformio/RFM69Socket/](../platformio/RFM69Socket/) — a container of related sketches:
`RawGateway`, `RawNode`, `RFM69_SocketTool`, `RFM69_Tool`.

---
Part of [ArduinoLibs](../README.md).
