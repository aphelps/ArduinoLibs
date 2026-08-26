#pragma once
//
// LoRa.h shim — host-build stand-in for sandeepmistry/arduino-LoRa, for the unit
// tests in this directory only. Never compiled into firmware.
//
// Modelled on the HardwareSerial shim in Arduino.h: the radio is a pair of packet
// queues, not a device. A test says "this frame arrived" and inspects "this frame
// was transmitted", so a case reads as a wire trace rather than as hardware
// choreography.
//
// Three knobs exist because the failures they model cannot be produced on a bench
// on demand:
//
//   * begin_result       — the radio not answering (wrong CS, wrong SPI bus, dead
//                          module). LoRa.begin() returns 0 when the version
//                          register does not read 0x12; RFM95Socket turns that into
//                          initialized() == false, and every later call must then
//                          be inert rather than talking to nothing.
//   * begin_packet_result — a send colliding with an async transmit still in
//                          flight, which the real driver reports by returning 0.
//   * feed_truncated()   — a frame whose announced length exceeds the bytes that
//                          actually arrive. parsePacket() returning more than the
//                          stream can supply is exactly the case where a length
//                          check that trusts the header reads past the buffer.
//
#include <stdint.h>
#include <stddef.h>
#include <deque>
#include <vector>

class TestLoRaClass {
 public:
  // ---- captured configuration -------------------------------------------------
  int  ss_pin = -99, reset_pin = -99, dio0_pin = -99;
  long frequency = 0;
  int  begin_calls = 0;
  int  set_pins_calls = 0;

  // ---- knobs ------------------------------------------------------------------
  int begin_result = 1;         // 1 = radio answered, 0 = it did not
  int begin_packet_result = 1;  // 1 = ready to send, 0 = busy

  // ---- captured transmissions -------------------------------------------------
  std::vector<std::vector<uint8_t> > tx_packets;
  bool in_packet = false;

  // ---- pending receptions -----------------------------------------------------
  // announced size is stored separately from the bytes so a truncated frame can be
  // expressed: parsePacket() reports `announced`, the stream holds fewer bytes.
  struct Incoming {
    int announced;
    std::vector<uint8_t> bytes;
  };
  std::deque<Incoming> rx_packets;
  std::vector<uint8_t> current;
  size_t current_pos = 0;

  int  last_rssi = -70;
  float last_snr = 9.5f;

  // ---- the arduino-LoRa API surface RFM95Socket uses ---------------------------
  void setPins(int ss, int reset, int dio0) {
    set_pins_calls++;
    ss_pin = ss; reset_pin = reset; dio0_pin = dio0;
  }

  int begin(long freq) {
    begin_calls++;
    frequency = freq;
    return begin_result;
  }

  int beginPacket(int = 0) {
    if (begin_packet_result == 0) return 0;
    in_packet = true;
    tx_packets.push_back(std::vector<uint8_t>());
    return 1;
  }

  size_t write(const uint8_t *buffer, size_t size) {
    if (!in_packet) return 0;
    for (size_t i = 0; i < size; i++) tx_packets.back().push_back(buffer[i]);
    return size;
  }

  int endPacket(bool = false) {
    in_packet = false;
    return 1;
  }

  int parsePacket(int = 0) {
    current.clear();
    current_pos = 0;
    if (rx_packets.empty()) return 0;
    Incoming in = rx_packets.front();
    rx_packets.pop_front();
    current = in.bytes;
    return in.announced;
  }

  int available() { return (int)(current.size() - current_pos); }

  int read() {
    if (current_pos >= current.size()) return -1;
    return current[current_pos++];
  }

  int   packetRssi() { return last_rssi; }
  float packetSnr()  { return last_snr; }

  // ---- test-side helpers ------------------------------------------------------
  void feed(const uint8_t *buf, size_t len) {
    Incoming in;
    in.announced = (int)len;
    in.bytes.assign(buf, buf + len);
    rx_packets.push_back(in);
  }

  // A frame that announces `announced` bytes but delivers only `len` of them.
  void feed_truncated(const uint8_t *buf, size_t len, int announced) {
    Incoming in;
    in.announced = announced;
    in.bytes.assign(buf, buf + len);
    rx_packets.push_back(in);
  }

  void test_reset() {
    ss_pin = reset_pin = dio0_pin = -99;
    frequency = 0;
    begin_calls = set_pins_calls = 0;
    begin_result = 1;
    begin_packet_result = 1;
    tx_packets.clear();
    in_packet = false;
    rx_packets.clear();
    current.clear();
    current_pos = 0;
  }
};

extern TestLoRaClass LoRa;
