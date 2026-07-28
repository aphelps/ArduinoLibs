EEPromUtils
===========

This library provides functions for more safely writing and reading from
EEPROM than the default Arduino EEPROM library.

When writing data to EEPROM it also adds a start byte, records the length
being written, and adds a CRC to the end of the data.  Additionally it only
writes a byte if the value to be written differs from what is present.

## Key API

Free functions (no class):

- `bool EEPROM_init()` — initialise the store (call once at setup).
- `bool EEPROM_commit()` — flush buffered writes (needed on ESP-class cores).
- `void EEPROM_end()`
- `int EEPROM_safe_write(int location, uint8_t *data, int datalen)` — write a wrapped
  record (start byte + length + CRC); returns the next free location or a negative error.
- `int EEPROM_safe_read(int location, uint8_t *buff, int bufflen)` — read a record written
  by `EEPROM_safe_write`, verifying the CRC.
- `boolean EEPROM_check_address(int location)`, `void EEPROM_dump(int location)`,
  `void EEPROM_shift(int start_address, int distance)`.

## Configuration

None. Each record carries `EEPROM_WRAPPER_SIZE` (3) bytes of overhead; size helpers
`EEPROM_SIZE(x)` / `EEPROM_DATA_SIZE(x)`. Error codes: `EEPROM_ERROR_END_EXCEEDED`,
`EEPROM_ERROR_NOT_START`, `EEPROM_ERROR_BAD_LEN`, `EEPROM_CRC_ERROR`.

## Dependencies

- External: Arduino `EEPROM` core library.

## Example

No standalone example sketch.

---
Part of [ArduinoLibs](../README.md).