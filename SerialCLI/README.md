SerialCLI
=========

A library for building simple tokenized CLIs using input over the serial 
interface.

## Key API

- `class SerialCLI`
  - `SerialCLI(byte maxLength, cliHandler_t handler)` — construct with the max input line
    length and a command handler callback.
  - `void checkSerial()` — call each loop; reads available serial input, and once a full
    line arrives, splits it into up to `MAX_TOKENS` (8) tokens and dispatches to the handler.
- `typedef void (*cliHandler_t)(char **tokens, byte numtokens)` — the user callback that
  receives the parsed tokens.

## Configuration

None (`MAX_TOKENS` is a fixed compile-time constant of 8).

## Dependencies

Arduino core only.

## Example

[../platformio/ExampleCLI/](../platformio/ExampleCLI/) (also `examples/ExampleCLI/`).

---
Part of [ArduinoLibs](../README.md).