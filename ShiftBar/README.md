# ShiftBar

## Purpose
A driver for ShiftBar / ShiftBrite RGB modules (Allegro A6281 constant-current drivers),
which chain over SPI-style clock/latch/data/enable pins and take 10-bit-per-channel colour.

## Key API
- `class ShiftBar`
  - `ShiftBar(uint8_t modules, uint16_t *values)` — number of chained modules and a caller
    supplied buffer of `modules * 3` colour values.
  - `void set(uint8_t module, uint16_t red, uint16_t green, uint16_t blue)` — set one
    module's colour.
  - `void set(uint16_t red, uint16_t green, uint16_t blue)` — set all modules.
  - `void update(void)` — clock the current values out to the chain.

Channel values are 10-bit: `0`–`SHIFTBAR_MAX` (1023).

## Configuration
The SPI-style pins are hard-coded `#define`s in `ShiftBar.h` (they are **not** `#ifndef`-guarded,
so a `-D` flag won't override them — edit the header to change a pin):

- `SHIFTBAR_CLOCK_PIN` (13, CI)
- `SHIFTBAR_ENABLE_PIN` (10, EI)
- `SHIFTBAR_LATCH_PIN` (9, LI)
- `SHIFTBAR_DATA_PIN` (11, DI)

Also defined: `SHIFTBAR_MAX` (1023) and the channel indices `SHIFTBAR_RED` / `SHIFTBAR_GREEN` /
`SHIFTBAR_BLUE`.

## Dependencies
Arduino core only.

## Example
No standalone example sketch.

---
Part of [ArduinoLibs](../README.md).
