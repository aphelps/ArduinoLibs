# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

A collection of Arduino/embedded C++ libraries targeting AVR (Arduino Nano, Moteino, 32u4) and ESP32 boards. Libraries are used via symlinks into the Arduino libraries directory (`../libraries/`) created by `setup.sh`. The primary build tool is **PlatformIO** (`pio`), not the Arduino IDE.

## Building and Uploading

Each `platformio/` subdirectory is a standalone PlatformIO project. Run these from inside the relevant project directory:

```bash
cd platformio/<ProjectName>

# Build for a specific board environment
pio run -e <env>

# Build and upload
pio run -e <env> --target upload

# Open serial monitor
pio device monitor

# Build with a flag override
pio run -e <env> -- --build-flag=-DDEBUG_LEVEL=3
```

Environment names are defined in each project's `platformio.ini` (e.g., `nano`, `esp32dev`, `moteino`).

The shared Arduino libraries directory is `/Users/amp/Dropbox/Arduino/libraries` — this path is hardcoded in `platformio.ini` files as `lib_dir`.

## Architecture

### Socket abstraction (`Socket/Socket.h`)

The central design pattern is an abstract `Socket` class that provides a transport-agnostic messaging interface. Three concrete implementations exist:

- **`RS485Utils/`** — RS485 serial bus using Nick Gammon's non-blocking protocol library
- **`RFM69Socket/`** — 433/915 MHz RF radio using the LowPowerLab RFM69 library
- **`XBeeSocket/`** — XBee RF modules

All socket implementations share the same API: `setup()`, `initBuffer()`, `sendMsgTo(address, data, length)`, `getMsg()`. Each has its own header struct (`rs485_socket_hdr_t`, `rfm69_socket_hdr_t`, `xbee_socket_hdr_t`) prepended to message payloads. `SOCKET_ADDR_ANY` is the broadcast address.

### Debug macros (`Debug/Debug.h`)

All debug output goes through compile-time macros gated on `DEBUG_LEVEL`. Set globally via `build_flags = -DDEBUG_LEVEL=<N>` in `platformio.ini`. Levels: `DEBUG_ERROR=1`, `DEBUG_LOW=2`, `DEBUG_MID=3`, `DEBUG_HIGH=4`, `DEBUG_TRACE=5`. Strings are stored in program memory (PROGMEM) via `F()` to avoid consuming limited RAM on AVR.

Per-file debug level override: define `DEBUG_LEVEL_<FILENAME>` and check for it before including `Debug.h`.

### PixelUtil (`PixelUtil/`)

Wrapper around FastLED that adds `pixel_range_t` structs (start+length pairs) and a `PRGB` color class. LED type and pin are configured entirely via compile flags in the format `PIXELS_<TYPE>_<datapin>[_clockpin]`, e.g., `PIXELS_WS2812B_3` or `PIXELS_APA102_12_8`. Use `BIG_PIXELS` flag to support >255 LEDs.

### MPR121 (`MPR121/`)

I2C capacitive touch sensor library (12 channels). Wraps the Sparkfun MPR121 driver and adds tap/double-tap detection and touch timing. Key compile flags: `IRQ_PIN`, `I2C_ADDRESS` (default `0x5A`), `TOUCH_TRIGGER`, `TOUCH_RELEASE`.

### Other libraries

- **`EEPromUtils/`** — EEPROM read/write with start byte, length, and CRC checking; only writes bytes that changed
- **`SerialCLI/`** — Tokenized serial command-line interface; register a `cliHandler_t` callback that receives `char **tokens, byte numtokens`
- **`Pins/`** — Generic pin abstraction with `SensorPin` and `OutputPin` subclasses plus action callbacks
- **`GeneralUtils/`** — Small utilities: non-blocking LED blink, PWM pin check, hex dump
- **`Shift/`, `ShiftBar/`** — Shift register I/O helpers

## PlatformIO project structure

The `platformio/` directory mirrors the library examples but each subdirectory is self-contained:
- `platformio.ini` points `src_dir` back to the example source (e.g., `../../MPR121/examples/MPR121BasicUse`)
- A `[DEFAULT]` section in `platformio.ini` defines `GLOBAL_BUILDFLAGS`, `GLOBAL_DEBUGLEVEL`, and `OPTION_FLAGS` that board-specific `[env:*]` sections interpolate with `%(GLOBAL_BUILDFLAGS)s`

## ESP32 notes

ESP32 targets use `platform = espressif32`, `board = esp32dev` or `esp32doit-devkit-v1`. Default I2C pins are SDA=GPIO21, SCL=GPIO22. Use `SERIAL_BAUD=115200` for ESP32. `SoftwareSerial` is unavailable on ESP32; RS485 must use hardware serial (`RS485_HARDWARE_SERIAL` flag).
