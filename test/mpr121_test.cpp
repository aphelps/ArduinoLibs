//
// mpr121_test — host unit tests for the MPR121 read-failure path.
//
// What this suite exists to pin
// -----------------------------
// MPR121::readTouchInputs() returns false both for "nothing changed" and for "the I2C read failed".
// A consumer therefore cannot tell a quiet panel from a dead one, and on a fire controller that
// distinction is the difference between a poof that cancels and a poof that sticks on: the remote
// module runs a repeat-forever blink program, so the cancel has to be actively transmitted on the
// release edge. Lose the release edge and nothing stops it.
//
// Worse than losing one edge, the driver used to lose ALL of them. The MPR121 holds its IRQ line
// asserted until a successful status read; the code cleared `triggered` before the read and did not
// restore it on failure, so after one failed read an edge-triggered IRQ could never deliver another
// edge. No further read was even ATTEMPTED, and touchStates kept its last good value forever.
//
// That last property is why these tests assert against Wire.request_count -- the number of I2C
// transactions actually attempted -- rather than against readOk() or `triggered`. A test that only
// checked the flags would pass on a build where the flags are set correctly but no retry ever
// happens, which is precisely the shape of the bug. The observable effect is bus traffic.
//
// The suite is deliberately silent about DEBUG output: DEBUG_LEVEL is left undefined here (the
// release configuration, as in the RS485 suite), so the DEBUG5 "Released pin" branch corrected
// alongside this change compiles out and cannot be asserted from the host.
//
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <string>

#include <Arduino.h>
#include <Wire.h>
#include "MPR121.h"

// Shim globals — see shim/Arduino.h and shim/Wire.h.
unsigned long test_millis_now = 0;
uint8_t       test_pin_state[64];
bool          test_malloc_should_fail = false;
std::string   test_debug_output;
char          close_line = 0;

// Debug.cpp is not linked in: it drags a board-specific error-LED path that has nothing to do with
// these tests. DEBUG_ERR_STATE calls are recorded instead, so an unexpected library error state is
// visible rather than silently swallowed.
int test_last_err_state = 0;
void debug_err_state(int state) { test_last_err_state = state; }
test_isr_t    test_attached_isr[4];
HardwareSerial Serial;
TwoWire        Wire;

static int failures = 0;
static int checks   = 0;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    checks++;                                                                  \
    if (!(cond)) {                                                             \
      printf("FAIL: %s (line %d)\n", (msg), __LINE__);                         \
      failures++;                                                              \
    }                                                                          \
  } while (0)

// The touch-status registers are little-endian: LSB carries electrodes 0-7.
static uint16_t pad_bit(uint8_t pad) { return (uint16_t)(1u << pad); }

// A sensor that has completed one successful read with `state` on the pads.
//
// useInterrupt is false on purpose. With it true, init() would take the AVR interrupt-pin switch
// and a pin that is not INTERUPT_0/1_PIN silently disables interrupts anyway; false makes
// readTouchInputs() call checkInterrupt(), which sets `triggered` from the (host-controlled) IRQ
// pin level. That gives the tests an explicit, honest way to raise an edge.
static void fresh_sensor(MPR121 &sensor, uint16_t state) {
  test_time_set(1000);
  test_pin_state[4] = 1;              // IRQ idle (active LOW)
  Wire.wire_reset();
  sensor.init(4 /* irqpin */, false /* useInterrupt */, 0x5A, false, false, false);

  // Raise an edge and satisfy it, so touchStates holds `state` and the edge is consumed.
  test_pin_state[4] = 0;
  Wire.wire_queue((uint8_t)(state & 0xFF), (uint8_t)(state >> 8));
  sensor.readTouchInputs();
  test_pin_state[4] = 1;
  sensor.readTouchInputs();           // idle call: consumes the edge
  Wire.wire_reset();
}

int main() {
  const uint8_t PAD = 3;

  // 1) A failed read is distinguishable from "nothing changed".
  //
  // Both return false. Before readOk() there was no way to tell them apart, which is the root of
  // the whole defect: a fire controller cannot fail safe on a condition it cannot observe.
  {
    MPR121 sensor;
    fresh_sensor(sensor, pad_bit(PAD));

    test_pin_state[4] = 1;                            // no edge
    CHECK(sensor.readTouchInputs() == false, "idle call reports no change");
    CHECK(sensor.readOk(), "idle call leaves read health good -- nothing was attempted");

    test_pin_state[4] = 0;                            // release edge arrives
    Wire.wire_fail_reads(true);
    CHECK(sensor.readTouchInputs() == false, "failed read also reports no change");
    CHECK(sensor.readFailed(), "but read health now reports the failure");
  }

  // 2) THE WEDGE. A failed read must still be retried on the next call.
  //
  // This is the assertion the whole fail-safe rests on. `triggered` is cleared before the
  // transaction (deliberately: an IRQ landing mid-read is preserved that way) and the chip holds
  // its IRQ asserted until a successful read, so without an explicit restore there is no second
  // edge and no second attempt -- forever.
  //
  // Asserted as bus traffic, not as a flag. On the unfixed driver request_count stops at 1.
  {
    MPR121 sensor;
    fresh_sensor(sensor, pad_bit(PAD));

    test_pin_state[4] = 0;
    Wire.wire_fail_reads(true);
    sensor.readTouchInputs();
    unsigned after_first = Wire.request_count;
    CHECK(after_first == 1, "the failing read was attempted once");

    // The IRQ line stays asserted (the chip is still waiting to be read) but NO new edge is
    // delivered -- that is what an edge-triggered IRQ does once its edge has been consumed.
    test_pin_state[4] = 1;
    sensor.readTouchInputs();
    sensor.readTouchInputs();
    CHECK(Wire.request_count > after_first,
          "a failed read is retried without needing a fresh IRQ edge (the wedge fix)");
  }

  // 3) Stale data is still stale -- the driver must not fabricate a release.
  //
  // The fix reports the failure; it does not invent an edge. Inventing one would be worse than the
  // bug: the controller would believe a release it never saw.
  {
    MPR121 sensor;
    fresh_sensor(sensor, pad_bit(PAD));
    CHECK(sensor.touched(PAD), "pad reads touched after the good read");

    test_pin_state[4] = 0;
    Wire.wire_fail_reads(true);
    sensor.readTouchInputs();

    CHECK(sensor.touched(PAD), "pad still reads touched -- the value is stale, not cleared");
    CHECK(!sensor.changed(PAD), "and no edge is fabricated from data that was never read");
  }

  // 4) changed() must not LATCH across repeated failed reads.
  //
  // This pins the prevStates placement. Syncing prevStates only on the success path would leave
  // prevStates != touchStates for the whole outage, so changed() would keep re-reporting the last
  // successful edge on every call. For checkPulse() -- `if (changed() && touched()) sendPulse()` --
  // that converts one stale reading into a poof command re-sent every loop, which is a worse
  // failure than the one being fixed. A failed read yields no new information; "no edge" is the
  // honest answer, and readOk() is what carries the bad news.
  {
    MPR121 sensor;
    fresh_sensor(sensor, 0);

    test_pin_state[4] = 0;                            // touch edge
    Wire.wire_queue((uint8_t)pad_bit(PAD), 0);
    CHECK(sensor.readTouchInputs(), "touch edge is delivered");
    CHECK(sensor.changed(PAD), "and reported as a change");

    Wire.wire_fail_reads(true);
    for (int i = 0; i < 5; i++) {
      sensor.readTouchInputs();
      CHECK(!sensor.changed(PAD), "no repeated stale edge while the read keeps failing");
    }
  }

  // 5) Recovery delivers the release edge that the fail-safe is protecting against losing.
  //
  // The retry from (2) is only worth having if the read that eventually succeeds still produces a
  // usable edge. Without the wedge fix this call never happens at all.
  {
    MPR121 sensor;
    fresh_sensor(sensor, pad_bit(PAD));

    test_pin_state[4] = 0;
    Wire.wire_fail_reads(true);
    sensor.readTouchInputs();
    CHECK(sensor.readFailed(), "read is failing");

    Wire.wire_fail_reads(false);                      // bus recovers; pad now released
    Wire.wire_queue(0, 0);
    CHECK(sensor.readTouchInputs(), "the retried read succeeds and reports a change");
    CHECK(sensor.readOk(), "read health recovers");
    CHECK(!sensor.touched(PAD), "pad now reads released");
    CHECK(sensor.changed(PAD), "and the release edge is delivered");
  }

  // 6) forceRead() makes "time since last successful read" a meaningful signal.
  //
  // Reads only happen when `triggered` is set, and `triggered` is set only by the IRQ. On an idle
  // panel no read is attempted at all, so a staleness clock would grow without bound during
  // perfectly normal operation and any fail-safe keyed on it would cancel poofs constantly. A
  // consumer needs a way to force the transaction.
  {
    MPR121 sensor;
    fresh_sensor(sensor, 0);

    test_pin_state[4] = 1;                            // idle: no edge, nothing to do
    sensor.readTouchInputs();
    CHECK(Wire.request_count == 0, "an idle panel attempts no I2C at all");

    sensor.forceRead();
    Wire.wire_queue(0, 0);
    sensor.readTouchInputs();
    CHECK(Wire.request_count == 1, "forceRead() attempts a transaction with no IRQ edge");
    CHECK(sensor.readOk(), "and a healthy device reports good health");

    sensor.forceRead();
    Wire.wire_fail_reads(true);
    sensor.readTouchInputs();
    CHECK(sensor.readFailed(), "a silent device is detectable on an idle panel");
  }

  // 7) The capability macro must exist, because a consumer's #error depends on it.
  //
  // Firmware that fails safe on a read failure guards itself with
  //   #if defined(FC_CAP_READ_FAILSAFE) && !defined(MPR121_HAS_READ_HEALTH) -> #error
  // Building that firmware against an older MPR121 would otherwise compile cleanly and ship a
  // fail-safe that can never fire, because the wedge it exists to catch also stops any read from
  // being attempted. Checked here so removing the macro breaks this suite and not only a
  // downstream build in another repository.
#ifndef MPR121_HAS_READ_HEALTH
#error "MPR121_HAS_READ_HEALTH must be defined by MPR121.h"
#endif
  CHECK(MPR121_HAS_READ_HEALTH == 1, "capability macro is exported for downstream #error guards");

  // 8) The touch/release tracing must actually reach the log.
  //
  // Built only in the DEBUG_LEVEL_MPR121=5 configuration (see the Makefile) because that is the
  // only one where this code exists at all. The release branch used to read
  //     } else { if (touched(i)) { ... "Released pin" ... } }
  // -- touched(i) is false in that branch by construction, so the line was unreachable and had
  // never printed once. A dead diagnostic is worse than no diagnostic: it makes a released pad look
  // identical to a pad that was never touched, which is exactly the distinction someone debugging a
  // stuck poof needs. Asserted on the captured output rather than by reading the source, since the
  // whole failure mode was that the source looked right.
#if defined(DEBUG_LEVEL_MPR121) && DEBUG_LEVEL_MPR121 >= 5
  {
    MPR121 sensor;
    fresh_sensor(sensor, 0);

    test_pin_state[4] = 0;
    Wire.wire_queue((uint8_t)pad_bit(PAD), 0);
    test_debug_clear();
    sensor.readTouchInputs();
    CHECK(test_debug_contains("Touched pin "), "a touch is traced");

    test_pin_state[4] = 0;
    Wire.wire_queue(0, 0);
    test_debug_clear();
    sensor.readTouchInputs();
    CHECK(test_debug_contains("Released pin "), "a release is traced (the branch used to be dead)");
  }
#endif

  printf("\n%d checks, %d failures\n", checks, failures);
  printf(failures ? "SOME TESTS FAILED\n" : "ALL TESTS PASSED\n");
  return failures ? 1 : 0;
}
