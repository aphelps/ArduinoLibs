# LCD

## Purpose
A thin wrapper over the Arduino `LiquidCrystal` library for a 16×2 character LCD, adding
automatic power-down of the display after a period of inactivity.

## Key API
Free functions plus a shared display instance:

- `void LCD_setup()` — initialise the display.
- `void LCD_set(int row, int col, String text, boolean pad)` — write `text` at
  `(row, col)`; `pad` blanks the rest of the line.
- `void LCD_loop()` — call each loop; turns the display off once it has been idle past the
  timeout (`lcd_disable_timeout`, 10000 ms) and back on when written to.
- `extern LiquidCrystal lcd` — the underlying display object, available for direct use.

## Configuration
None (no `-D` flags). The `LiquidCrystal` pin wiring (`5, 6, 7, 8, 9, 10`) and the 10 s idle
timeout are hardcoded in `LCD.cpp`; edit the source to change them.

## Dependencies
- External: Arduino `LiquidCrystal`.

## Example
No standalone example sketch.

---
Part of [ArduinoLibs](../README.md).
