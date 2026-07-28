# Shift

## Purpose
A driver for 74HC595-style serial-in/parallel-out shift registers, letting a few MCU pins
drive many digital outputs.

## Key API
- `class Shift`
  - `Shift(byte clock, byte latch, byte data, byte registers)` — construct with the three
    control pins and the number of chained registers.
  - `void SetBit(byte bit, boolean on)` — set/clear one output bit in the buffer.
  - `void Write()` — shift the buffer out and latch it.

## Configuration
None. Note: the internal output buffer is fixed at 4 bytes (`_data[4]`, flagged in the
source as a to-be-generalised limitation), so it drives up to 32 output bits.

## Dependencies
Arduino core only.

## Example
No standalone example sketch. `Pins`' `Output` can drive a `Shift` channel (see
[../Pins/](../Pins/)).

---
Part of [ArduinoLibs](../README.md).
