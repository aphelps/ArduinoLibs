# PixelUtil

## Purpose
A wrapper over **FastLED** for addressable RGB strips (WS2812B, APA102, WS2801). It adds
pixel ranges, colour helpers, and a few built-in patterns on top of FastLED's buffer, and
selects the LED type and pins entirely through compile-time flags.

## Key API
- `class PixelUtil` — the strip.
  - `PixelUtil(uint16_t numPixels, uint8_t dataPin, uint8_t clockPin, uint8_t order=RGB)`
    and `void init(...)` with the same arguments.
  - `void setPixelRGB(uint16_t led, byte r, byte g, byte b)` (+ `uint32_t color` / `CRGB`
    / `PRGB*` overloads)
  - `void setAllRGB(byte r, byte g, byte b)` / `void setAllRGB(uint32_t color)`
  - `void setRangeRGB(pixel_range_t range, CRGB crgb)`
  - `void setBrightness(uint8_t brightness)`
  - `uint16_t numPixels()`, `uint32_t getColor(uint16_t led)`, `void update()`
  - Built-in patterns: `patternLoop(pattern, size, periodms)`, `patternOne`, `patternRed`,
    `patternGreen`, `patternBlue`.
- `class PRGB` — a small RGB colour holder (`setColor`, `incrColor`, `color`, `getCRGB`).
  Marked in the source for eventual removal in favour of FastLED's `CRGB`.
- Free colour helpers: `pixel_color`, `pixel_red/green/blue`, `pixel_wheel`,
  `pixel_primary`, `pixel_secondary`, `pixel_heat`, `pixel_heat_discrete`, `fadeTowards`.

## Configuration
Two ways to select the LED type and pins, both resolved in `PixelUtil_config.h`:

1. **Generic** — set the type + pin(s) directly:
   `-DPIXELS_TYPE=PIXELS_TYPE_WS2812B -DPIXELS_DATA=10 -DPIXELS_CLOCK=6`
   (`PIXELS_TYPE_*` = `WS2812B` / `APA102` / `WS2801`; `PIXELS_CLOCK` only for clocked types).
2. **Named shortcut** — one `PIXELS_<type>_<datapin>[_<clockpin>]` flag that the config maps to
   the generic form. Only combinations explicitly listed in `PixelUtil_config.h` exist; current
   examples:
   - `-DPIXELS_WS2812B_12` — WS2812B on data pin 12 (single-wire types take one pin).
   - `-DPIXELS_APA102_12_8` — APA102 on data pin 12, clock pin 8.
   - `-DPIXELS_WS2801_5_7` — WS2801 on data pin 5, clock pin 7 (hardware SPI).

With no type flag set, the config emits a `#warning` and falls back to WS2801 on data pin 12,
clock pin 8.

Other flags:
- `BIG_PIXELS` — widen the pixel address type to `uint16_t` for strips > 255 LEDs.
- `PIXEL_NUM_OVERRIDE` — override the pixel count passed at construction.
- `DEBUG_LEVEL_PIXELUTIL` — a PixelUtil-specific debug level.

## Dependencies
- External: **FastLED**.

## Example
[../platformio/PixelExample/](../platformio/PixelExample/) (`PixelExample.ino`).

---
Part of [ArduinoLibs](../README.md).
