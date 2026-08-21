#pragma once
//
// Wire.h shim — host-build stand-in, for the unit tests in this directory only.
//
// Never compiled into firmware. It exists so MPR121.cpp can be built and exercised by a plain host
// compiler, which is what makes the read-failure path testable without an I2C bus you can physically
// pull apart.
//
// The one thing here that is load-bearing rather than convenience:
//
//   * requestFrom() delivers however many bytes the scripted response holds, NOT however many were
//     asked for. The defect under test is entirely about a SHORT read — MPR121::readTouchInputs()
//     branches on `Wire.available() < 2` — so a shim that always satisfied the request would make the
//     failure path unreachable and the tests would pass against the bug.
//
// wire_fail_reads() is the induced fault: it makes every subsequent requestFrom deliver nothing, the
// host-side equivalent of yanking the sensor's SDA stub off the bus mid-touch.
//
#include <stdint.h>
#include <deque>
#include <vector>

class TwoWire {
 public:
  // --- test-side controls -------------------------------------------------------------------
  bool                 fail_reads   = false;  // deliver 0 bytes for every request
  unsigned             request_count = 0;     // requestFrom() calls -- i.e. attempted transactions
  std::deque<uint8_t>  response;              // bytes the next request(s) will deliver
  std::vector<uint8_t> written;               // everything the library wrote

  void wire_reset() {
    fail_reads = false; request_count = 0;
    response.clear(); written.clear(); rx.clear();
  }
  void wire_fail_reads(bool fail) { fail_reads = fail; }
  void wire_queue(uint8_t lsb, uint8_t msb) { response.push_back(lsb); response.push_back(msb); }

  // --- Arduino TwoWire surface --------------------------------------------------------------
  void begin() {}
  void beginTransmission(uint8_t addr) { last_address = addr; }
  size_t write(uint8_t b) { written.push_back(b); return 1; }
  uint8_t endTransmission() { return 0; }
  uint8_t endTransmission(bool) { return 0; }

  uint8_t requestFrom(uint8_t addr, uint8_t quantity) {
    last_address = addr;
    request_count++;
    rx.clear();
    if (fail_reads) return 0;                 // short read: nothing arrives
    uint8_t n = 0;
    while (n < quantity && !response.empty()) {
      rx.push_back(response.front());
      response.pop_front();
      n++;
    }
    return n;
  }
  uint8_t requestFrom(uint8_t addr, uint8_t quantity, uint8_t) {
    return requestFrom(addr, quantity);
  }

  int available() { return (int)rx.size(); }
  int read() {
    if (rx.empty()) return -1;
    uint8_t b = rx.front();
    rx.pop_front();
    return b;
  }

  uint8_t last_address = 0;

 private:
  std::deque<uint8_t> rx;
};

extern TwoWire Wire;
