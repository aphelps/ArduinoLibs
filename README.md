# ArduinoLibs

A collection of reusable Arduino/embedded C++ libraries for building networked
LED/lighting and sensor installations. These are the shared building blocks used across
the HMTL ecosystem — the HMTL firmware, the WLED **ampworks** usermods (which vendor a copy
of this repo), and the HMTL_Fire_Control controllers all depend on libraries defined here.

Each top-level directory is a single self-contained library (one primary header, optional
`.cpp`). They range from tiny helpers (`Debug`, `GeneralUtils`) to a small networking stack
(the `Socket` family) and hardware drivers (`MPR121`, `PixelUtil`, `Shift`).

## Installation

The libraries are consumed as plain Arduino libraries — one symlink per library into your
Arduino `libraries/` directory:

```sh
git clone https://github.com/aphelps/ArduinoLibs.git
cd ArduinoLibs
./setup.sh          # symlinks every top-level dir into ../libraries/<name>
```

`setup.sh` symlinks **every** top-level directory (the libraries, and `platformio/`) into an
Arduino `libraries/` directory one level above the repo. Restart the Arduino IDE afterward so
it picks up the new libraries.

### Building the examples (PlatformIO)

`platformio/` holds PlatformIO projects for the example/test sketches — e.g. `DebugExample`,
`ExampleCLI`, `MPR121BasicUse` / `MPR121Multiple`, `PixelExample`, `RS485SocketTool`,
`SocketTestMaster` / `SocketTestSlave`, `XBeeSocketTest`. Most are a single project with their
own `platformio.ini` (with `esp32`/`nano` environments where relevant); a few are containers of
related sub-projects (e.g. `RFM69Socket/` holds `RawGateway`, `RawNode`, `RFM69_SocketTool`,
`RFM69_Tool`). Each is the runnable demo/reference for the corresponding library:

```sh
cd platformio/RS485SocketTool
pio run -e esp32 --target upload
pio device monitor -e esp32
```

### Debugging

Most libraries print through the `Debug` macros, gated by a compile-time `DEBUG_LEVEL` —
`#define DEBUG_LEVEL <n>` before including `Debug.h` (see the **Debug** entry below).

## The Socket family (messaging transports)

`Socket/` defines an abstract **`Socket`** base class — a common message-passing API — and
four concrete transports implement it, so higher-level code can send/receive addressed
messages without caring which radio/wire carries them:

| Library | Transport | Underlying driver |
|---------|-----------|-------------------|
| `Socket` | abstract base (API only, header-only) | — |
| `RS485Utils` (`RS485Socket`) | RS485 two-wire bus | Nick Gammon's `RS485_non_blocking` |
| `RFM69Socket` | RFM69 sub-GHz radio | LowPowerLab `RFM69` |
| `RFM95Socket` | RFM95 LoRa radio (915 MHz) | `sandeepmistry/arduino-LoRa` |
| `XBeeSocket` | XBee / ZigBee radio | `XBee` |

All four declare `: public Socket` and override the same virtual API:

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

Addresses are `socket_addr_t`; `SOCKET_ADDR_ANY` is the broadcast address and
`SOCKET_ADDR_INVALID` the sentinel. Each transport adds its own on-wire header struct
(`rs485_socket_hdr_t` / `rfm69_socket_hdr_t` / `rfm95_socket_hdr_t` / `xbee_socket_hdr_t`), a
`*_BUFFER_TOTAL(n)` macro for sizing the send buffer, and (for RFM69/RFM95/XBee) a `*_DATA_LENGTH(n)`
for the inverse. Because they share the interface, a device can swap one transport for another with
minimal code change.

`RFM95Socket` differs from the others in one way worth knowing before reading its code: **LoRa has no
node addressing of its own.** RS485 frames carry a destination, and the RFM69 driver owns both a node
address and a broadcast address; arduino-LoRa is a raw packet pipe, so every node on the frequency
receives every packet and `rfm95_socket_hdr_t.address` plus the software filter in `getMsg()` is the
*entire* addressing mechanism. It is also the only transport here with **no encryption** — the RFM69
has AES in hardware and `RFM69Socket` uses it; the RFM95 does not, and no software cipher is applied.
See `RFM95Socket/README.md`.

**The RS485, RFM69, RFM95 and XBee header structs are `__attribute__((__packed__))` with
`static_assert`s pinning their size and every field offset — 7, 6, 6 and 5 bytes respectively.** This is a correctness
requirement, not
a space optimisation. `sizeof(hdr)` is what positions the payload at both ends of the link, and these
structs mix `byte` with 16-bit `socket_addr_t`, so an unpacked declaration is one size on AVR
(alignment 1) and another on any 2-byte-aligned ABI. An AVR module and a 32-bit node would then
disagree about where the payload starts and misparse every frame in both directions —
`rfm69_socket_hdr_t` worse still, since its padding is *interior* and moves the addresses themselves.
Packing pins the AVR layout, which is the one deployed modules already speak; it is layout-neutral on
AVR, and the assertions say so rather than leaving it to be trusted. **Anything added to these structs
must keep the attribute and extend the assertions.**

Not yet covered: `TCPSocket`'s `tcp_socket_hdr_t` is packed already but carries no assertions, and
`tcp_socket_msg_t` is unpacked. There is no cross-ABI hazard there today — that transport is
`#if defined(ESP32)`-only, so it never meets an AVR peer, and a packed header gives the outer struct
alignment 1 regardless. Worth folding into the same pattern if TCPSocket ever gains a second target.

## Library reference

### Core utilities

- **[Debug](Debug/)** — Compile-time-leveled serial debug macros. Activate by `#define DEBUG_LEVEL <n>`
  (`NONE`…`TRACE`) before including `Debug.h`; higher-numbered `DEBUGn_*` calls compile to
  nothing below their level. Provides `DEBUGn_PRINT/VALUE/HEX/COMMAND`, `DEBUG_ERR`,
  `debug_err_state()`, `debug_print_memory()`, `print_hex_buffer()`. (Format strings live in
  PROGMEM. A legacy `DEBUG_PRINT(v,x)` macro block is deprecated.)
- **[GeneralUtils](GeneralUtils/)** — Small miscellaneous helpers: `blink_value()` (non-blocking, single-pin —
  uses statics), `pin_is_PWM()` (hardcoded for ATmega328/Nano-class pins), `print_hex_string()`.
- **[EEPromUtils](EEPromUtils/)** — Safe EEPROM records with a start byte, length, and CRC, plus
  wear-minimizing writes. Free functions (no class): `EEPROM_init()`, `EEPROM_commit()`,
  `EEPROM_safe_write()`/`EEPROM_safe_read()`, `EEPROM_check_address()`, `EEPROM_dump()`,
  `EEPROM_shift()`. ~3 bytes of wrapper overhead per record (`EEPROM_WRAPPER_SIZE`).
- **[SerialCLI](SerialCLI/)** — Tokenizing serial command-line interface. `SerialCLI::checkSerial()` parses
  input into up to `MAX_TOKENS` (8) tokens and dispatches to a user `cliHandler_t` callback.

### Networking (Socket family)

- **[Socket](Socket/)** — the abstract base described above (header-only; virtuals are non-pure). Carries
  `recvLimit`, `sourceAddress`, `send_buffer`, `send_data_size`; `SOCKET_ADDRESS_MATCH` helps
  address filtering.
- **[RS485Utils](RS485Utils/)** (`RS485Socket`) — RS485 transport. Defaults to `SoftwareSerial` unless
  `RS485_HARDWARE_SERIAL` is set (use hardware `Serial1`/`Serial2` on ESP32); `DEFAULT_BAUD`
  28000; helpers `printSocketMsg()`/`printBuffer()`. Some `*_FROM_DATA` macros are deprecated.
  See the `RS485SocketTool` example (`RS485Utils/examples/RS485SocketTool/`) for a working reference.
- **[RFM69Socket](RFM69Socket/)** — RFM69 radio transport. Adds `setEncryptionKey()`; maps the RFM69 broadcast
  address to `SOCKET_ADDR_ANY`.
- **[XBeeSocket](XBeeSocket/)** — XBee/ZigBee transport; wraps an `XBee*` and a `ZBRxResponse`.

### Pixels & shift-register output

- **[PixelUtil](PixelUtil/)** (`PixelUtil`, `PRGB`) — Wrapper over **FastLED** adding pixel ranges, color
  helpers (`pixel_wheel`, `pixel_heat`, `fadeTowards`, …) and built-in patterns
  (`setPixelRGB`/`setAllRGB`/`setRangeRGB`/`setBrightness`/`update`/`patternLoop`). Configured
  entirely by compile flags: `PIXELS_<type>_<datapin>[_clockpin]`, `BIG_PIXELS`,
  `PIXEL_NUM_OVERRIDE`, `DEBUG_LEVEL_PIXELUTIL`. The FastLED type/pin macro expansions live in
  `PixelUtil_config.h`. (Author intends to eventually drop `PRGB` in favor of FastLED's `CRGB`.)
- **[Shift](Shift/)** (`Shift`) — 74HC595 shift-register driver: `SetBit()`, `Write()`. Internal buffer is
  fixed at 4 bytes (noted as a to-be-generalized limitation), so it drives a bounded number of
  registers.
- **[ShiftBar](ShiftBar/)** (`ShiftBar`) — ShiftBar/ShiftBrite RGB module driver over SPI-style pins; 10-bit
  color (`SHIFTBAR_MAX` 1023); SPI pins set via `SHIFTBAR_*_PIN` defines.

### Input & display

- **[Pins](Pins/)** (`Pin`, `Sensor`, `Output`) — Generic pin abstraction with debounced/analog/pull-up
  sensors and (optionally shift-register-backed) outputs, driven by `pin_action_t` callbacks:
  `checkSensors()`, `triggerOutputs()`, `action_set_output()`, `action_print_value()`. `Output`
  can drive a **`Shift*`** (sibling dep). Note: the header guard is `SENSORS_H`; the generic
  pin-action functions live in `Actions.cpp`.
- **[MPR121](MPR121/)** (`MPR121`, `MPR121_State`) — Driver for the MPR121 12-channel I²C capacitive touch
  sensor (12 electrodes + 1 proximity at index 12). `touched()`/`changed()`/`getFiltered()`/
  `getBaseline()`/`setThreshold()`; `MPR121_State` adds gesture helpers (`checkTapped`/
  `checkHeld`/`checkReleased`, incl. double-tap / long-touch). Call `Wire.begin()` before init;
  includes a per-board interrupt-pin mapping (incl. ESP32).
- **[LCD](LCD/)** — Thin wrapper over Arduino `LiquidCrystal` for a 16×2 display with auto power-down
  after 10 s idle: `LCD_setup()`, `LCD_set()`, `LCD_loop()`, `extern LiquidCrystal lcd`. Pins and
  geometry are hardcoded in the `.cpp`.
- **[Menu](Menu/)** (`MenuItem`, `Menu`) — LCD-driven menu with selectable items and `menu_action_t`
  callbacks: `next`/`prev`/`enter`/`action`/`display`. Drives a `LiquidCrystal*` directly (not
  the `LCD` sibling library).

## Dependency notes

- Sibling dependencies within this repo are minimal: the three transports include `Socket.h`;
  `Pins`' `Output` can drive `Shift`. Everything else depends only on the Arduino core or an
  external library (`FastLED`, `LiquidCrystal`, `RFM69`, `XBee`, `RS485_non_blocking`).
- Many libraries are configured through compile-time flags rather than runtime constructors —
  see each library's section for the relevant `-D` defines.

## License

MIT (see individual source headers). Author: Adam Phelps.
