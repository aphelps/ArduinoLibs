#pragma once
//
// FastLED.h shim — host-build stand-in, for the unit tests in this directory only.
//
// Never compiled into firmware. It exists so PixelUtil.cpp can be built and exercised by a plain
// host compiler. FastLED itself is unbuildable here: it is a large template library that reaches
// straight at AVR/ESP registers to bit-bang the pixel clock, and none of that is what PixelUtil's
// own logic (ranges, clamping, colour helpers) needs in order to be tested.
//
// This is deliberately more than `CRGB` + `fill_solid`. Merely *constructing* a PixelUtil runs
// PIXELS_ADD() -> FastLED.addLeds<...>(leds, num_pixels), so the singleton, the chipset tags, the
// colour-order enumerators and the correction constants all have to exist. In particular RGB must
// be visible at header-parse time, not just at the call site: PixelUtil.h:73,75 uses `uint8_t
// order=RGB` as a *default argument*.
//
// What this shim does NOT do: drive any hardware, honour colour order, or apply correction.
// setCorrection()/setBrightness()/show() record their arguments so a test can assert they were
// called, and nothing more. Anything that depends on FastLED's actual output belongs on a bench,
// not here.
//
#include <stdint.h>
#include <stddef.h>

#include "Arduino.h"

// ---------------------------------------------------------------------------------------------
// CRGB
// ---------------------------------------------------------------------------------------------
// Layout is load-bearing, not incidental. `CRGB *leds` is indexed with a pixel number and handed
// to fill_solid as a raw span, so a shim whose CRGB were padded to 4 bytes would place the
// out-of-bounds write this suite exists to pin at a different offset from the one the firmware
// produces -- the test would still pass while measuring the wrong thing. The static_asserts below
// pin the same property HMTL's cross-ABI layout guard rests on.
struct CRGB {
  uint8_t r;
  uint8_t g;
  uint8_t b;

  CRGB() : r(0), g(0), b(0) {}
  CRGB(uint8_t ir, uint8_t ig, uint8_t ib) : r(ir), g(ig), b(ib) {}

  // Non-explicit on purpose: PixelUtil assigns a packed 0xRRGGBB into a CRGB slot
  // (setPixelRGB(uint16_t, uint32_t)) and passes one to fill_solid (setAllRGB(uint32_t)).
  CRGB(uint32_t colorcode)
    : r((uint8_t)((colorcode >> 16) & 0xFF)),
      g((uint8_t)((colorcode >>  8) & 0xFF)),
      b((uint8_t)( colorcode        & 0xFF)) {}

  // setDistinct() writes a single colour channel as `leds[led / 3][led % 3] = value`.
  uint8_t &operator[](uint8_t x)       { return (&r)[x]; }
  uint8_t  operator[](uint8_t x) const { return (&r)[x]; }

  bool operator==(const CRGB &rhs) const { return r == rhs.r && g == rhs.g && b == rhs.b; }
  bool operator!=(const CRGB &rhs) const { return !(*this == rhs); }
};

static_assert(sizeof(CRGB) == 3, "CRGB must be exactly 3 bytes, as FastLED's is");
#if __cplusplus >= 201103L
static_assert(alignof(CRGB) == 1, "CRGB must have alignment 1, as FastLED's does");
#endif

inline void fill_solid(CRGB *targetArray, int numToFill, const CRGB &color) {
  for (int i = 0; i < numToFill; i++) targetArray[i] = color;
}

// ---------------------------------------------------------------------------------------------
// Colour order and correction
// ---------------------------------------------------------------------------------------------
enum EOrder {
  RGB = 0012,
  RBG = 0021,
  GRB = 0102,
  GBR = 0120,
  BRG = 0201,
  BGR = 0210
};

enum LEDColorCorrection {
  TypicalSMD5050    = 0xFFB0F0,
  TypicalPixelString = 0xFFE08C,
  UncorrectedColor  = 0xFFFFFF
};

// Only ever appears inside a PIXELS_DEFINE*_RATE macro, which is expanded only when
// PIXEL_DATA_RATE is set. Present so the header parses under any flag combination.
#define DATA_RATE_MHZ(X) (((uint32_t)1000000 * (X)))

// ---------------------------------------------------------------------------------------------
// Chipset tags
// ---------------------------------------------------------------------------------------------
// Plain tag types rather than FastLED's class templates. PixelUtil only ever *names* the chipset
// as the first template argument of addLeds; nothing here instantiates a controller, and modelling
// the real template signatures (which differ between clocked and single-wire parts) would buy
// nothing but a chance for the shim's overload set to become ambiguous.
struct WS2812B {};
struct WS2801  {};
struct APA102  {};

// ---------------------------------------------------------------------------------------------
// The FastLED singleton
// ---------------------------------------------------------------------------------------------
// Three addLeds forms, matching the three shapes PixelUtil_config.h can expand to:
//   PIXELS_DEFINE0 -> addLeds<CHIPSET, ORDER>              (SPI, no pins named)
//   PIXELS_DEFINE1 -> addLeds<CHIPSET, DATA, ORDER>        (single-wire, e.g. WS2812B)
//   PIXELS_DEFINE2 -> addLeds<CHIPSET, DATA, CLOCK, ORDER> (clocked, e.g. APA102/WS2801)
// Exactly one is instantiated per build -- whichever the PIXELS_* flag selects.
class CFastLED {
 public:
  // Recorded so a test can assert the strip was actually registered, and so a stray call is
  // visible rather than silently swallowed.
  CRGB    *last_leds     = NULL;
  int      last_count    = 0;
  int      add_calls     = 0;
  uint32_t last_correction = 0;
  uint8_t  last_brightness = 255;
  int      show_calls    = 0;

  template <typename CHIPSET, EOrder ORDER>
  void addLeds(CRGB *data, int nLeds) { record(data, nLeds); }

  template <typename CHIPSET, uint8_t DATA_PIN, EOrder ORDER>
  void addLeds(CRGB *data, int nLeds) { record(data, nLeds); }

  template <typename CHIPSET, uint8_t DATA_PIN, uint8_t CLOCK_PIN, EOrder ORDER>
  void addLeds(CRGB *data, int nLeds) { record(data, nLeds); }

  void setCorrection(uint32_t c) { last_correction = c; }
  void setBrightness(uint8_t b)  { last_brightness = b; }
  void show()                    { show_calls++; }

 private:
  void record(CRGB *data, int nLeds) {
    last_leds  = data;
    last_count = nLeds;
    add_calls++;
  }
};

extern CFastLED FastLED;
