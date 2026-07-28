# Pins

## Purpose
A generic pin abstraction for inputs and outputs: debounced/analog/pull-up sensors and
optionally shift-register-backed outputs, wired together through `pin_action_t` callbacks.

## Key API
- `class Pin` — common base (pin number, analog flag).
- `class Sensor : public Pin` — an input.
  - `Sensor(byte pin, boolean pull_up, boolean analog, boolean reversed, pin_action_t action, void *action_arg)`
    (plus lighter-weight overloads)
  - `int read(void)`, `int debouncedRead(void)` (default debounce `DEFAULT_DEBOUNCE_DELAY` 50 ms)
- `class Output : public Pin` — an output.
  - `Output(byte pin, byte value)` / `Output(byte pin, byte value, Shift *shift)` /
    `Output(byte pin, byte value, Shift *shift, Sensor *sensor)`
  - `void setValue(byte value)`, `void trigger(void)`
- Free functions:
  - `boolean checkSensors(Pin **pins, byte num_pins, boolean debounce)` — poll sensors, fire actions.
  - `void triggerOutputs(Pin **pins, byte num_pins)` — apply pending output values.
  - `void action_set_output(int pin, int value, void *arg)` and
    `void action_print_value(int pin, int value, void *arg)` — ready-made `pin_action_t`
    callbacks (defined in `Actions.cpp`).

## Configuration
None. Note: the header guard is `SENSORS_H` (historical name).

## Dependencies
- Sibling: `Shift` — an `Output` can drive a shift-register channel via a `Shift*`.
- External: Arduino core.

## Example
No standalone example sketch.

---
Part of [ArduinoLibs](../README.md).
