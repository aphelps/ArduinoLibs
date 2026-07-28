# GeneralUtils

## Purpose
A small grab-bag of miscellaneous helpers shared across the other libraries — a
non-blocking status blink, a PWM-capability check, and a hex string printer.

## Key API
Free functions (no class):

- `void blink_value(int pin, int value, int period_ms, int idle_periods)` — blinks
  `value` times on `pin`, then idles for `idle_periods` before repeating. Non-blocking:
  it keeps its progress in statics, so it drives a single pin only.
- `boolean pin_is_PWM(int pin)` — true if the pin supports `analogWrite`. The pin table
  is hardcoded for ATmega328 / Nano-class boards.
- `void print_hex_string(const byte *bytes, int len)` — prints `len` bytes as hex to
  `Serial`.

## Configuration
None.

## Dependencies
Arduino core only.

## Example
No standalone example sketch; these helpers are used throughout the other libraries.

---
Part of [ArduinoLibs](../README.md).
