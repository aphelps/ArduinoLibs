#pragma once
//
// SPI.h shim — host-build stand-in, for the unit tests in this directory only.
//
// Exists so RFM95Socket.cpp can be built by a plain host compiler. It records the
// arguments to begin() rather than ignoring them, and that is the point: the ESP32
// four-argument form SPI.begin(sck, miso, mosi, ss) is what binds the bus to the
// radio's pins, and getting it wrong is the single likeliest way to end up with a
// silently dead RFM95 on this board (the default VSPI pins reach the pixel clock
// instead). A test can only pin that if the shim remembers what it was told.
//
#include <stdint.h>

class SPIClass {
 public:
  int  begin_calls = 0;
  int  sck = -99, miso = -99, mosi = -99, ss = -99;

  void begin(int8_t _sck = -1, int8_t _miso = -1, int8_t _mosi = -1, int8_t _ss = -1) {
    begin_calls++;
    sck = _sck; miso = _miso; mosi = _mosi; ss = _ss;
  }
  void end() {}

  void test_reset() { begin_calls = 0; sck = miso = mosi = ss = -99; }
};

extern SPIClass SPI;
