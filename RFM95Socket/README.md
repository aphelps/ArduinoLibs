# RFM95Socket

A `Socket` implementation over the RFM95 (LoRa) radio, using
[sandeepmistry/arduino-LoRa](https://github.com/sandeepmistry/arduino-LoRa).

Built for the **SparkFun ESP32 LoRa 1-CH Gateway** (WRL-15006), which carries an ESP32-WROOM-32
and an RFM95W on one board. ESP32-only: it is listed in `[common] esp_only_libs` in
`HMTL/platformio/HMTL_Module/platformio.ini`, so the AVR envs ignore it.

## Pin map — read this before wiring anything

Verified against the **schematic netlist**, not a reading of the schematic PDF and not SparkFun's
example sketches. Source: `sparkfunX/ESP32_LoRa_1CH_Gateway`, `Hardware/esp32_lora_gateway.sch`
(EAGLE 9 schematics are XML, so the nets can be read directly).

| Role | ESP32 GPIO | Net | Evidence |
|---|---|---|---|
| SCK | 14 | `SCK` | `U1.SCK_H/IO14 <-> U2.SCK, J4.3` |
| MISO | 12 | `MISO` | `U1.MISO_H/IO12 <-> U2.MISO, J4.2` |
| MOSI | 13 | `MOSI` | `U1.MOSI_H/IO13 <-> U2.MOSI, J4.1` |
| CS / NSS | 16 | `RFM_CS` | `U1.IO16 <-> U2.NSS` |
| DIO0 | 26 | `RFM_INT/26` | `U1.IO26 <-> U2.DIO0` |
| DIO1 | 33 | `RFM_DIO1/33` | `U1.XTAL_N/IO33 <-> U2.DIO1` |
| DIO2 | 32 | `RFM_DIO2/32` | `U1.XTAL_P/IO32 <-> U2.DIO2` |
| RESET | **not connected** | `RFM_RST` | `U2.!RESET <-> R5.1` only; `R5.2` is on `3.3V` |

These are the defaults in `RFM95Socket.h` (`RFM95_SPARKFUN_1CH_*`), so on this board you need not
pass any pins at all.

### Two traps, both of which produce the same symptom

`initialized()` returning false is the only thing either of these tells you, which is why they are
documented here rather than left to a bench session to rediscover.

**1. RESET is not wired to the ESP32 — pass `-1`.** Net `RFM_RST` reaches the module's `!RESET` and a
10 k pull-up to 3.3 V and stops. No GPIO is on that net; the radio cannot be hardware-reset in
software on this board. `LoRa.cpp` guards its reset sequence on `_reset != -1`, so `-1` is the
correct value and not a placeholder.

SparkFun's own code disagrees with itself here, *because the value is never used*:

| Sketch | `.rst` |
|---|---|
| `sparkfun/…/Firmware/ESP-1CH-TTN-Device-ABP.ino` | 5 |
| `sparkfunX/…/Firmware/ESP-1CH-TTN-Device-ABP.ino` | 5 |
| `sparkfunX/…/Production/ESP32_Lora_Send_sketch.ino` | 27 |
| `sparkfunX/…/Production/Receiver/…_Receiver_sketch.ino` | 27 |

Copying either would drive a real GPIO for no reason — 5 and 27 are both broken out to the headers,
and 5 is a strapping pin.

**2. SPI must be bound explicitly — the default is wrong and actively harmful.** `LoRa.begin()` calls
`_spi->begin()` on the global `SPI`, which under `board = esp32dev` is VSPI: SCK 18 / MISO 19 /
MOSI 23 / SS 5. None of those reach the radio. Worse, GPIO 23 is `PIXELS_CLOCK` in
`[env:esp32_lora_gw]`, so the default configuration drives the pixel clock line while failing to
talk to the radio.

`RFM95Socket::setup()` therefore calls `SPI.begin(14, 12, 13, 16)` before `LoRa.begin()`. SparkFun's
examples never hit this because their board variant (`sparkx_esp32_lora/pins_arduino.h`) redefines
`SS`/`MOSI`/`MISO`/`SCK` to the radio's pins; we do not build with that variant.

## Usage

```cpp
#include <SPI.h>
#include <LoRa.h>
#include <Socket.h>
#include <RFM95Socket.h>

RFM95Socket radio;
byte buffer[128];
byte *data;

void setup() {
  radio.init(0x0042);          // our address; frequency defaults to 915 MHz (US)
  radio.setup();               // binds SPI, brings up the radio
  if (!radio.initialized()) {
    // Wrong CS, wrong SPI bus, or a dead module. It cannot tell you which.
  }
  data = radio.initBuffer(buffer, sizeof(buffer));
}

void loop() {
  unsigned int len;
  const byte *msg = radio.getMsg(&len);   // filtered to our address + broadcast
  if (msg) { /* ... */ }

  data[0] = 0xAB;
  radio.sendMsgTo(0x0043, data, 1);
}
```

For a non-default board:

```cpp
radio.init(0x0042, 868000000L);   // EU band
radio.setRadioPins(cs, reset, dio0);
radio.setSPIPins(sck, miso, mosi);
radio.setup();                    // both setters must precede setup()
```

## Addressing

**LoRa has no node addressing.** Every node on the frequency receives every packet. `RS485Socket`
gets a destination from the frame, and `RFM69Socket` gets one from its driver (plus
`RF69_BROADCAST_ADDR`, which is why it needs `RFM69_BROADCAST_CONVERT`). Here, `rfm95_socket_hdr_t`
carries the address and `getMsg()`'s `SOCKET_ADDRESS_MATCH` is the *entire* filter — remove it and
every node acts on every packet.

`SOCKET_ADDR_ANY` needs no conversion in either direction: with no driver-level broadcast address to
map onto, it travels as itself.

## Encryption: there is none

This transport is **unencrypted**, deliberately and by omission of hardware rather than by oversight.
The RFM69 has AES-128 on the chip and `RFM69Socket::setEncryptionKey()` uses it; the SX1276 in the
RFM95 has no equivalent, and no software cipher is applied here. Anything sensitive must be encrypted
by the caller before `sendMsgTo()`.

## On-wire header

```c
typedef struct __attribute__((__packed__)) {
  byte          ID;       // offset 0
  socket_addr_t source;   // offset 1
  socket_addr_t address;  // offset 3
  byte          flags;    // offset 5
} rfm95_socket_hdr_t;     // 6 bytes
```

Same field set and size as `rfm69_socket_hdr_t`. Packed, with `static_assert`s on the size **and
every field offset**, so an AVR peer and an ESP32 peer cannot disagree about where the payload
starts. There is no AVR LoRa node today — that is prophylaxis for one appearing later, and the host
suite compiles this header a second time under `-fpack-struct=1` (the AVR-layout proxy) to keep the
claim checked rather than merely intended.

Deliberately **generic**: LoRa↔RS485/HMTL bridging is planned, so nothing radio-specific (RSSI, SNR,
spreading factor) goes on the wire, where a bridge would have to strip it. Link quality is available
from `packetRssi()` / `packetSnr()` instead — and is *not* used as a health check anywhere, because a
healthy RSSI on a packet that failed its address filter says the radio works and says nothing about
whether addressing does.

Maximum payload is **249 bytes** (255-byte LoRa frame minus the 6-byte header). `Socket::sendMsgTo`
takes `const byte length` and `Socket::getLength` returns `byte`, so the base class already pins a
payload to one byte; there is no field here to widen for a larger MTU.

`sendMsgTo()` **drops** a payload over 249 bytes rather than clamping it, and this is load-bearing:
`RFM95_BUFFER_TOTAL()` casts to `uint8_t`, so 250 wraps to a total of 0 and 255 wraps to 5 — an
empty frame and a runt respectively, both of which a peer running this code rejects while the sender
reports success. Clamping would be worse than dropping, because a truncated-but-well-formed frame is
undetectable at the far end. `RFM69Socket` has no equivalent guard and needs none: its 61-byte
ceiling puts the sum nowhere near 256.

## Tests

Host tests, no radio needed:

```bash
make -C ArduinoLibs/test          # or `make test-libs` from the super-repo
```

Two binaries are built from one source. `rfm95_socket_test` is the shipping default
(`DEBUG_LEVEL` self-defaults to `DEBUG_TRACE`); `rfm95_socket_test_quiet` compiles the tracing out.
The second is not symmetry: `RFM69Socket`'s runt-frame length check sits inside
`#if DEBUG_LEVEL == DEBUG_TRACE` and so is absent from the build that ships. Moving this library's
check into the same `#if` was tried as a mutation — the trace binary stayed green and only the quiet
binary caught it.

`make -C ArduinoLibs/test rfm95-layout` compiles the header under the host ABI and under
`-fpack-struct=1`. Note which half does the work: deleting `__attribute__((__packed__))` fails 5
static_asserts under the **default ABI** and **zero** under `-fpack-struct=1`, because that flag packs
every struct in the translation unit and so reconstructs by flag exactly what the attribute was meant
to guarantee. The default-ABI compile is what pins the packing; the AVR-proxy compile only confirms
the asserted offsets match what AVR would produce.

## Known gaps

- **No acks, retries or duplicate suppression.** Deliberate: those belong above `Socket` so every
  transport gets them, not inside one radio's library. RadioHead was evaluated and rejected for this
  work — `RHReliableDatagram` has exactly these features, but only for its own radios, and adopting
  it would mean taking its **8-bit** address space against `socket_addr_t`'s 16-bit one.
- **No interrupt-driven receive.** `getMsg()` polls `LoRa.parsePacket()`. DIO0 is wired (GPIO 26) and
  `LoRa.onReceive()` exists, so this is available when polling proves insufficient.
- **`SPI.begin()` is a no-op if the bus is already up.** The ESP32 core's `SPIClass::begin()` opens
  with `if (_spi) return;`, so if anything brings up the global `SPI` before `RFM95Socket::setup()`,
  the explicit pins here are silently discarded. The host suite cannot catch this — `shim/SPI.h`
  records pins on every call, which is more forgiving than the hardware. Relevant the moment this
  meets WLED, whose `bus_manager` is also an SPI consumer.
- **Not integrated into WLED.** This is the library only.
