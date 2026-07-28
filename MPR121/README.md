MPR121
======

This library enables interaction with the MPR121 chip, a 12-channel capacitive
touch chip which communicates over I2C.

Additionally it provides a class to track interactions with the capacitive touch
sensors, including detecting taps, double-taps, and tracking the time between
touches.

Acknowledgements
----------------
This code was initially based on the MPR121 example code provided by Sparkfun: https://github.com/sparkfun/MPR121_Capacitive_Touch_Breakout/tree/master/Firmware/MPR121Q/Arduino%20Sketch


Future work
-----------
* Integrate additional functionality from the Adafruit library (https://github.com/adafruit/Adafruit_MPR121_Library)
* Access to non-autoleveled data
* Deal with the simulated 13th channel proximity sensor

## Key API

- `class MPR121` — the sensor (12 electrodes + 1 proximity channel at index 12).
  - `MPR121(byte irqpin, boolean interrupt, boolean times)` plus overloads taking the I²C
    `address` and a `filtered` flag (which allocates the filtered-data buffer). Default I²C
    address is `START_ADDRESS` (0x5A). **`Wire.begin()` must be called before construction.**
  - `boolean touched(byte sensor)`, `boolean changed(byte sensor)`
  - `uint8_t getBaseline(byte sensor)`, `uint16_t getFiltered(byte sensor)`,
    `boolean getBaselineAll()`, `boolean getFilteredAll()`
  - `void setThreshold(byte sensor, byte trigger, byte release)`,
    `void setThresholds(byte trigger, byte release)`
- `class MPR121_State` — gesture tracking over an `MPR121`.
  - `MPR121_State(MPR121 *sensor, uint16_t sensor_map)`
  - `boolean checkTapped(byte s)`, `boolean checkReleased(byte s)`, `boolean checkHeld(byte s)`
    (includes double-tap / long-touch handling).

## Configuration

None via `-D` flags — the IRQ pin, interrupt mode, I²C address, and options are constructor
arguments. Includes a per-board interrupt-pin mapping (including ESP32).

## Dependencies

- External: Arduino `Wire` (I²C).

## Example

[../platformio/MPR121BasicUse/](../platformio/MPR121BasicUse/) and
[../platformio/MPR121Multiple/](../platformio/MPR121Multiple/) (also under `examples/`).

---
Part of [ArduinoLibs](../README.md).