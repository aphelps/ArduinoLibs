# Testing `RS485SocketTool` on an ESP32

A self-serve guide to bring up and verify the `RS485SocketTool` example on real hardware.
The sketch is a bidirectional `RS485Socket` client: once running it auto-sends a 2-byte
message once per second and auto-prints anything it receives. It is **non-interactive** —
there are no serial commands to type.

## 1. Parts needed

| Part | Notes |
|------|-------|
| ESP32 dev board | `esp32doit-devkit-v1` (the `esp32` env's board) or any ESP32 devkit |
| RS485 transceiver | **MAX3485 / SP3485 / SN65HVD3xx (3.3 V) recommended** — direct-connects to the ESP32. A 5 V **MAX485** works too but needs a level shifter on its `RO` output (see §2). |
| Jumper wires + breadboard | For the 5 signal/power connections below |
| A second RS485 node *(for the two-node test)* | Another ESP32 (or a 5 V AVR) + transceiver, or a USB-RS485 dongle. Optional for the single-node smoke test. |
| 2 × 120 Ω resistors *(optional)* | Bus termination for cable runs longer than ~1–2 m |

## 2. Wiring (ESP32 ↔ transceiver ↔ RS485 bus)

The `esp32` build uses hardware `Serial2` with these pins (from `platformio/RS485SocketTool/platformio.ini`):

```
ESP32 GPIO16 (RX2) → transceiver RO   (Receiver Output → ESP32 receive)
ESP32 GPIO17 (TX2) → transceiver DI   (Driver Input   → ESP32 transmit)
ESP32 GPIO18       → transceiver RE + DE (tie together; HIGH = transmit, LOW = receive)
transceiver A      → RS485 bus A (non-inverting)
transceiver B      → RS485 bus B (inverting)
transceiver VCC    → 3.3 V   (see level-shift note if using a 5 V MAX485)
transceiver GND    → common ground (shared with every node on the bus)
```

Build flags that set the above (already in the `esp32` env): `-DRS485_HARDWARE_SERIAL=2`,
`-DPIN_RS485_RECV=16`, `-DPIN_RS485_XMIT=17`, `-DPIN_RS485_ENABLED=18`.

**Level shifting.** The ESP32 is 3.3 V logic. A 3.3 V transceiver (MAX3485/SP3485/SN65HVD3xx)
connects directly. A 5 V **MAX485**'s `RO` output swings to 5 V and **will damage** the ESP32
GPIO — put a logic-level shifter (or series-resistor + zener clamp) on the `RO → GPIO16` line.
`DI`/`DE` inputs accept the ESP32's 3.3 V HIGH, so those lines are fine unshifted.

**Termination.** For cables over ~1–2 m, place a 120 Ω resistor between `A` and `B` at each
end of the bus. Short bench setups usually don't need it.

**Mixed voltages.** RS485 is differential — a 3.3 V node and a 5 V node share the same `A`/`B`
pair with no adapter; each transceiver converts the bus signal to its own logic level.

## 3. Build & flash

From the example's PlatformIO dir:

```
cd platformio/RS485SocketTool
pio run -e esp32 --target upload
pio device monitor -e esp32          # 115200 baud
```

## 4. Single-node smoke test

With just one node powered and the serial monitor open, confirm:

1. At boot: `*** RS485SocketTool initialized ***`
2. Then, once per second: `* Sending N` — `N` increments each second (it is `count`, a byte,
   so it wraps 0→255→0).

Seeing those two confirms the firmware runs, `Serial` is at the right baud, and the
transmit path is being driven. (With no second node, "Sending" lines are expected;
"Received" lines only appear once another node is transmitting — see §5.)

## 5. Two-node test (send + receive)

1. Flash a second node. It can be another ESP32 on the `esp32` env, or an AVR board on the
   `nano` env (`pio run -e nano --target upload`) with that board's RS485 pins wired.
2. Wire both transceivers to the **same** bus: `A`↔`A`, `B`↔`B`, and a **common ground**
   between the two nodes.
3. Each node broadcasts (`sendMsgTo(SOCKET_ADDR_ANY, …)`) once per second, so each should
   print the other's traffic. On each monitor, confirm lines like:

   ```
   * Received data 2: 54 01
   ```

   i.e. a `* Received data <len>: <hex bytes>` line appearing about once per second, in step
   with the other node's `* Sending N`. (The 2 payload bytes are `'T'` = `0x54` and the
   sender's `count`.)

Both nodes default to RS485 address 128; because the tool broadcasts to `SOCKET_ADDR_ANY`,
receive works without assigning distinct addresses. (Override per node with `-DADDRESS=<n>`
if you want addressed instead of broadcast traffic.)

## 6. Troubleshooting

| Symptom | Likely cause |
|---------|--------------|
| No serial output at all | Wrong baud (must be 115200) or wrong serial port selected in the monitor |
| `Sending` but never `Received` (two-node) | `A`/`B` swapped, no common ground between nodes, or the other node isn't transmitting |
| Garbage / corrupted bytes | `A`/`B` reversed, missing common ground, or long unterminated cable (add 120 Ω) |
| ESP32 resets / erratic when bus active | 5 V `RO` driving GPIO16 without a level shifter (see §2) — fix before continuing |
| Nothing transmits | `RE`/`DE` not tied together, or not wired to GPIO18 |

## Notes
- Exact strings emitted by the sketch (`RS485SocketTool.ino`): `*** RS485SocketTool initialized ***`
  (boot), `* Sending N` (every `SEND_PERIOD` = 1000 ms), `* Received data <len>: <hex>` (on receipt).
- The sketch prints via `DEBUG1_*` macros; the `esp32` env builds with `-DDEBUG_LEVEL=1`, so these
  lines are compiled in.
- There are **no interactive/serial commands** — the tool runs autonomously once flashed.
