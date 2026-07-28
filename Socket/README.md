# Socket

## Purpose
An abstract base class defining a common message-passing API for addressed communication.
Concrete transports (RS485, RFM69, XBee) subclass it so higher-level code can send and
receive addressed messages without caring which radio or wire carries them. Header-only.

## Key API
`class Socket` declares the virtual interface every transport overrides (virtuals are
non-pure):

```cpp
virtual void          setup();
virtual boolean       initialized();
virtual byte         *initBuffer(byte *data, uint16_t data_size);
virtual void          sendMsgTo(uint16_t address, const byte *data, const byte length);
virtual const byte   *getMsg(unsigned int *retlen);
virtual const byte   *getMsg(uint16_t address, unsigned int *retlen);
virtual byte          getLength();
virtual void         *headerFromData(const void *data);
virtual socket_addr_t sourceFromData(void *data);
virtual socket_addr_t destFromData(void *data);
```

Public members carried by the base: `recvLimit`, `sourceAddress`, `send_buffer`,
`send_data_size`.

Addresses are `socket_addr_t`; `SOCKET_ADDR_ANY` (-1) is the broadcast address and
`SOCKET_ADDR_INVALID` (-2) the sentinel. The `SOCKET_ADDRESS_MATCH(x, y)` macro helps with
address filtering.

## Configuration
None.

## Dependencies
Arduino core only. See the transports built on this base:
[RS485Utils](../RS485Utils/), [RFM69Socket](../RFM69Socket/), [XBeeSocket](../XBeeSocket/).

## Example
The transport examples exercise this API — e.g.
[../platformio/SocketTestMaster/](../platformio/SocketTestMaster/) and
[../platformio/SocketTestSlave/](../platformio/SocketTestSlave/).

---
Part of [ArduinoLibs](../README.md).
