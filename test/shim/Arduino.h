#pragma once
//
// Arduino.h shim — host-build stand-in, for the unit tests in this directory only.
//
// Never compiled into firmware. It exists so RS485Utils.cpp and RS485_non_blocking.cpp can be built
// and exercised by a plain host compiler, which is what makes the receive-path state machine testable
// without hardware.
//
// Two things here are deliberate, not incidental:
//
//   * millis() is backed by a settable counter (see test_time_set / test_time_advance below) rather
//     than the wall clock. The packet-timeout tests need to jump time forward by a known amount and
//     get the same answer every run; a real clock would make them flaky and slow.
//
//   * HardwareSerial is a queue, not a device. Tests push the exact byte sequence a peer would put on
//     the wire and then let the library read it back, so a test case reads as "these bytes arrived"
//     rather than "some hardware did something".
//
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <deque>

typedef uint8_t  byte;
typedef bool     boolean;

#define HIGH   1
#define LOW    0
#define INPUT  0
#define OUTPUT 1

// ---------------------------------------------------------------------------------------------
// Controllable clock
// ---------------------------------------------------------------------------------------------
// Not static: the tests link against these to drive time explicitly.
extern unsigned long test_millis_now;
inline unsigned long millis() { return test_millis_now; }
inline void test_time_set(unsigned long ms)     { test_millis_now = ms; }
inline void test_time_advance(unsigned long ms) { test_millis_now += ms; }

// ---------------------------------------------------------------------------------------------
// Forceable malloc failure
// ---------------------------------------------------------------------------------------------
// RS485::begin() allocates the receive buffer with malloc and does not check the result. That failure
// cannot be induced on a bench on demand, so the test forces it here: set the flag, call setup(), and
// the next allocation returns NULL exactly as a real out-of-memory would.
//
// Scope is narrow on purpose. The macro only rewrites malloc() calls in translation units that include
// this shim — in practice the one call in RS485_non_blocking::begin(). Everything else in the test
// (std::deque, std::vector) allocates through operator new and is untouched, and free() still receives
// a genuine malloc pointer because the non-failing path delegates straight to the real allocator.
extern bool test_malloc_should_fail;
inline void test_malloc_fail_next(bool fail) { test_malloc_should_fail = fail; }
inline void *test_malloc(size_t n) {
  if (test_malloc_should_fail) return NULL;
  return malloc(n);
}
#define malloc(n) test_malloc(n)

// ---------------------------------------------------------------------------------------------
// map()
// ---------------------------------------------------------------------------------------------
// The Arduino core's integer re-range, transcribed exactly (including its truncating division and
// its lack of clamping). PixelUtil.cpp's colour helpers -- pixel_primary, pixel_secondary,
// pixel_heat, pixel_heat_discreet -- call it, so without this the whole file fails to compile with
// "use of undeclared identifier 'map'". Not a behavioural stand-in: it is the same expression the
// core uses, so those helpers pick the same colour here as on a device.
inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// ---------------------------------------------------------------------------------------------
// GPIO stubs
// ---------------------------------------------------------------------------------------------
// RS485Socket drives the DE/RE enable pin around every transmission. Recorded rather than ignored so
// a test can assert the pin was actually asserted, and so a stray write is visible.
extern uint8_t test_pin_state[64];
inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t pin, uint8_t val) { if (pin < 64) test_pin_state[pin] = val; }
inline int  digitalRead(uint8_t pin) { return (pin < 64) ? test_pin_state[pin] : 0; }

// ---------------------------------------------------------------------------------------------
// HardwareSerial as a pair of byte queues
// ---------------------------------------------------------------------------------------------
// rx  — what the test says arrived on the wire; the library reads from here.
// tx  — what the library wrote; the test inspects it.
class HardwareSerial {
 public:
  std::deque<uint8_t> rx;
  std::deque<uint8_t> tx;
  bool flushed = false;

  void begin(unsigned long) {}
  int  available() { return (int)rx.size(); }
  int  read() {
    if (rx.empty()) return -1;
    uint8_t b = rx.front();
    rx.pop_front();
    return b;
  }
  size_t write(uint8_t b) { tx.push_back(b); return 1; }
  void   flush() { flushed = true; }

  // Test-side helpers.
  void feed(uint8_t b)                        { rx.push_back(b); }
  void feed(const uint8_t *buf, size_t len)   { for (size_t i = 0; i < len; i++) rx.push_back(buf[i]); }
  void clear()                                { rx.clear(); tx.clear(); flushed = false; }
};

extern HardwareSerial Serial;
