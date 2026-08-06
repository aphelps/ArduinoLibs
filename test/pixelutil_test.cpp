//
// pixelutil_test.cpp — host unit tests for PixelUtil's bounds behaviour.
//
// Build/run:  make -C ArduinoLibs/test        (or `make test-libs` from the super-repo)
//
// NOT `make -C ArduinoLibs test`: there is no Makefile at the ArduinoLibs root, so that exits 0
// having run nothing -- a false SUCCESS indistinguishable from a passing suite.
//
// This file is compiled TWICE, into pixelutil_test (default, PIXEL_ADDR_TYPE = uint8_t) and
// pixelutil_test_big (-DBIG_PIXELS, uint16_t). The defect it pins wraps to a different length in
// each width -- 66 vs 65346 for {num=10, start=200} -- and only the second is catastrophic, so both
// are built and both are run.
//
// The defect: setRangeRGB clamped only the END of a range. A start past the end of the strip made
// `numPixels() - range.start` wrap, and fill_solid walked off the array from `leds + start`:
//
//   | build                     | clamped length | writes           |
//   |---------------------------|----------------|------------------|
//   | default (uint8_t)         | 66             | leds[200..265]   |
//   | -DBIG_PIXELS (uint16_t)   | 65346          | leds[200..65545] |
//
// How the corruption is caught: `leds` is private and allocated inside init(), so the test cannot
// wrap its own sentinels around the buffer. Instead the global operator new[]/delete[] are replaced
// in this TU. While `g_tracking` is armed (only around the PixelUtil construction), the allocation
// is padded and sentinel-filled on both sides, and sentinels_intact() is asserted after every case.
//
// THE TRAILING PAD SIZE IS THE WHOLE BALLGAME. This bug does not walk off the end of the buffer --
// it starts writing at `leds + start`, i.e. 600 bytes past the base of a 30-byte allocation, so a
// conventional 8/16/32-byte guard band sits entirely inside the gap the write skips over and
// survives untouched. Measured against the buggy implementation: PAD=16 intact, PAD=64 intact,
// PAD=512 intact, PAD=1024 caught. The pad is therefore sized to the whole reachable overrun,
// (max start + max clamped length) * sizeof(CRGB): 1530 bytes for uint8_t, 393210 for uint16_t.
// One 384 KiB pad serves both widths -- free on a host, and deterministic rather than dependent on
// heap layout (ASan caught the small case too, but a write this far out can land in another live
// allocation and go unreported; the pad cannot miss).
//
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "PixelUtil.h"

// ---------------------------------------------------------------------------------------------
// Shim globals (declared extern in test/shim/Arduino.h and test/shim/FastLED.h)
// ---------------------------------------------------------------------------------------------
unsigned long  test_millis_now = 0;
uint8_t        test_pin_state[64];
bool           test_malloc_should_fail = false;
HardwareSerial Serial;
CFastLED       FastLED;

// ---------------------------------------------------------------------------------------------
// operator new[] interposition
// ---------------------------------------------------------------------------------------------
// Program-wide replacement, gated behind g_tracking so only the PixelUtil `new CRGB[num_pixels]`
// pays the 384 KiB and the sentinel check has exactly one allocation to report on.
//
// NB the suite builds -std=c++11, where sized deallocation does not exist, so replacing
// operator delete[](void*) alone is sufficient. If the standard is ever raised, operator
// delete[](void*, size_t) must be replaced too or the default one will free() an interior pointer.
static bool           g_tracking       = false;
static unsigned char *g_raw            = NULL;  // base of the padded malloc block
static unsigned char *g_user           = NULL;  // interior pointer handed to the program
static size_t         g_user_size      = 0;
static int            g_tracked_allocs = 0;

static const size_t FRONT_PAD = 64;      // underruns start at the pointer, so small is enough
static const size_t TRAIL_PAD = 393216;  // 384 KiB -- covers the whole reachable overrun, see above
static const unsigned char SENTINEL = 0xA5;

void *operator new[](size_t n) {
  if (!g_tracking) {
    void *p = malloc(n ? n : 1);
    if (p == NULL) abort();
    return p;
  }
  unsigned char *raw = (unsigned char *)malloc(FRONT_PAD + n + TRAIL_PAD);
  if (raw == NULL) abort();
  memset(raw, SENTINEL, FRONT_PAD);
  memset(raw + FRONT_PAD + n, SENTINEL, TRAIL_PAD);
  g_raw       = raw;
  g_user      = raw + FRONT_PAD;
  g_user_size = n;
  g_tracked_allocs++;
  return g_user;
}

void operator delete[](void *p) noexcept {
  if (p == NULL) return;
  if (p == g_user) {
    free(g_raw);
    g_raw = g_user = NULL;
    return;
  }
  free(p);
}

static bool sentinels_intact() {
  if (g_user == NULL) return false;
  for (size_t i = 0; i < FRONT_PAD; i++) {
    if (g_raw[i] != SENTINEL) return false;
  }
  const unsigned char *tail = g_user + g_user_size;
  for (size_t i = 0; i < TRAIL_PAD; i++) {
    if (tail[i] != SENTINEL) return false;
  }
  return true;
}

// ---------------------------------------------------------------------------------------------
// Harness (style matches rs485_receive_test.cpp: one TU, a CHECK macro, no framework)
// ---------------------------------------------------------------------------------------------
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

static const uint16_t NUM = 10;

// A baseline no test colour equals, so "unchanged" and "not flooded" are distinguishable.
static const uint32_t BASELINE = 0x102030;

static void paint_baseline(PixelUtil &pixels) {
  pixels.setAllRGB((byte)0x10, (byte)0x20, (byte)0x30);
}

static int count_pixels_with(PixelUtil &pixels, uint32_t color) {
  int n = 0;
  for (uint16_t i = 0; i < NUM; i++) {
    if (pixels.getColor(i) == color) n++;
  }
  return n;
}

int main(void) {
  printf("PIXEL_ADDR_TYPE width: %u byte(s)%s\n",
         (unsigned)sizeof(PIXEL_ADDR_TYPE),
#ifdef BIG_PIXELS
         " (BIG_PIXELS)"
#else
         ""
#endif
         );

  // -------------------------------------------------------------------------------------------
  // Construction, with allocation tracking armed only around it.
  // dataPin MUST be 3: the suite builds -DPIXELS_WS2812B_3 and PIXELS_ADD() only registers the
  // strip (initialized = true) when the pin matches.
  // -------------------------------------------------------------------------------------------
  g_tracking = true;
  static PixelUtil pixels(NUM, 3, 4);
  g_tracking = false;

  CHECK(g_tracked_allocs == 1, "exactly one tracked allocation (the leds array)");
  CHECK(g_user != NULL, "leds allocation was interposed");
  CHECK(g_user_size == (size_t)NUM * sizeof(CRGB), "leds allocation is numPixels() * sizeof(CRGB)");
  CHECK(FastLED.add_calls == 1, "PIXELS_ADD registered the strip");
  CHECK(FastLED.last_leds == (CRGB *)g_user, "addLeds received the tracked buffer");
  CHECK(FastLED.last_count == NUM, "addLeds received the pixel count");
  CHECK(pixels.numPixels() == NUM, "numPixels() reports the constructed count");
  CHECK(sentinels_intact(), "sentinels intact after construction");

  // -------------------------------------------------------------------------------------------
  // THE regression: start far past the end writes nothing and corrupts nothing.
  // Buggy code wrote leds[200..265] (uint8_t) / leds[200..65545] (uint16_t) here.
  // -------------------------------------------------------------------------------------------
  {
    paint_baseline(pixels);
    pixel_range_t range;
    range.start  = 200;
    range.length = 5;
    pixels.setRangeRGB(range, CRGB(0xFF, 0x00, 0x00));
    CHECK(count_pixels_with(pixels, BASELINE) == NUM, "start=200,len=5: no pixel was written");
    CHECK(sentinels_intact(), "start=200,len=5: sentinels intact (buggy code corrupts them)");
  }

#ifdef BIG_PIXELS
  // A start only representable at uint16_t width.
  {
    paint_baseline(pixels);
    pixel_range_t range;
    range.start  = 1000;
    range.length = 5;
    pixels.setRangeRGB(range, CRGB(0xFF, 0x00, 0x00));
    CHECK(count_pixels_with(pixels, BASELINE) == NUM, "start=1000,len=5: no pixel was written");
    CHECK(sentinels_intact(), "start=1000,len=5: sentinels intact");
  }
#endif

  // -------------------------------------------------------------------------------------------
  // Boundary: start == num exactly. THE BEHAVIOUR CHANGE, asserted deliberately: the old code
  // clamped this to length = 0 and fell into "fill the whole strip" -- an accidental flood.
  // Now it is a no-op. A sender using an out-of-range start as "all pixels" goes dark.
  // -------------------------------------------------------------------------------------------
  {
    paint_baseline(pixels);
    pixel_range_t range;
    range.start  = NUM;
    range.length = 5;
    pixels.setRangeRGB(range, CRGB(0xFF, 0x00, 0x00));
    CHECK(count_pixels_with(pixels, BASELINE) == NUM,
          "start==num: no-op, NOT a whole-strip flood (behaviour change from the clamp-to-0 path)");
    CHECK(count_pixels_with(pixels, 0xFF0000) == 0, "start==num: nothing painted red");
    CHECK(sentinels_intact(), "start==num: sentinels intact");
  }

  // -------------------------------------------------------------------------------------------
  // Boundary: start == num-1. Writes exactly the last pixel, however long the range.
  // -------------------------------------------------------------------------------------------
  {
    paint_baseline(pixels);
    pixel_range_t range;
    range.start  = NUM - 1;
    range.length = 5;
    pixels.setRangeRGB(range, CRGB(0x00, 0xFF, 0x00));
    CHECK(pixels.getColor(NUM - 1) == 0x00FF00, "start==num-1: last pixel written");
    CHECK(count_pixels_with(pixels, BASELINE) == NUM - 1, "start==num-1: all others untouched");
    CHECK(sentinels_intact(), "start==num-1: sentinels intact");
  }

  // -------------------------------------------------------------------------------------------
  // Boundary: start + length == num exactly. Writes start..num-1, nothing more.
  // -------------------------------------------------------------------------------------------
  {
    paint_baseline(pixels);
    pixel_range_t range;
    range.start  = 4;
    range.length = 6;  // 4 + 6 == 10
    pixels.setRangeRGB(range, CRGB(0x00, 0x00, 0xFF));
    for (uint16_t i = 4; i < NUM; i++) {
      CHECK(pixels.getColor(i) == 0x0000FF, "start+len==num: pixel inside the range written");
    }
    for (uint16_t i = 0; i < 4; i++) {
      CHECK(pixels.getColor(i) == BASELINE, "start+len==num: pixel before the range untouched");
    }
    CHECK(sentinels_intact(), "start+len==num: sentinels intact");
  }

  // -------------------------------------------------------------------------------------------
  // An interior range touches only itself.
  // -------------------------------------------------------------------------------------------
  {
    paint_baseline(pixels);
    pixel_range_t range;
    range.start  = 2;
    range.length = 3;  // pixels 2, 3, 4
    pixels.setRangeRGB(range, CRGB(0xFF, 0xFF, 0x00));
    CHECK(count_pixels_with(pixels, 0xFFFF00) == 3, "interior range: exactly 3 pixels written");
    CHECK(pixels.getColor(1) == BASELINE && pixels.getColor(5) == BASELINE,
          "interior range: neighbours untouched");
    CHECK(sentinels_intact(), "interior range: sentinels intact");
  }

  // -------------------------------------------------------------------------------------------
  // length == 0 means the WHOLE strip -- 0..num-1, regardless of start, NOT start..num-1.
  // (HMTLprotocol.py zero-fills unspecified program bytes, so a colour program sent with just an
  // RGB triple arrives as {0, 0}; this semantic is load-bearing and now checked on the input,
  // not produced by accident of the clamp arithmetic.)
  // -------------------------------------------------------------------------------------------
  {
    paint_baseline(pixels);
    pixel_range_t range;
    range.start  = 0;
    range.length = 0;
    pixels.setRangeRGB(range, CRGB(0x40, 0x50, 0x60));
    CHECK(count_pixels_with(pixels, 0x405060) == NUM, "len==0,start==0: whole strip filled");
    CHECK(sentinels_intact(), "len==0,start==0: sentinels intact");

    paint_baseline(pixels);
    range.start  = 7;  // a non-zero start must not change the meaning
    range.length = 0;
    pixels.setRangeRGB(range, CRGB(0x40, 0x50, 0x60));
    CHECK(count_pixels_with(pixels, 0x405060) == NUM,
          "len==0,start==7: whole strip filled (0..num-1, not start..num-1)");
    CHECK(sentinels_intact(), "len==0,start==7: sentinels intact");
  }

  // -------------------------------------------------------------------------------------------
  // length larger than the strip clamps to the end.
  // -------------------------------------------------------------------------------------------
  {
    paint_baseline(pixels);
    pixel_range_t range;
    range.start  = 0;
    range.length = 200;
    pixels.setRangeRGB(range, CRGB(0x01, 0x02, 0x03));
    CHECK(count_pixels_with(pixels, 0x010203) == NUM, "len>num: whole strip filled");
    CHECK(sentinels_intact(), "len>num: sentinels intact");

    paint_baseline(pixels);
    range.start  = 6;
    range.length = 200;
    pixels.setRangeRGB(range, CRGB(0x01, 0x02, 0x03));
    CHECK(count_pixels_with(pixels, 0x010203) == NUM - 6, "start=6,len=200: clamped to 6..num-1");
    CHECK(pixels.getColor(5) == BASELINE, "start=6,len=200: pixel before the range untouched");
    CHECK(sentinels_intact(), "start=6,len=200: sentinels intact");
  }

  // -------------------------------------------------------------------------------------------
  // getColor() is bounds-checked: out-of-range reads return 0, not heap garbage.
  // -------------------------------------------------------------------------------------------
  {
    paint_baseline(pixels);
    CHECK(pixels.getColor(NUM - 1) == BASELINE, "getColor in range reads the pixel");
    CHECK(pixels.getColor(NUM) == 0, "getColor(num) returns 0");
    CHECK(pixels.getColor(0xFFFF) == 0, "getColor(65535) returns 0");
    CHECK(sentinels_intact(), "getColor probing: sentinels intact");
  }

  // -------------------------------------------------------------------------------------------
  // setDistinct() is bounds-checked: valid led range is 0 .. num*3-1.
  // -------------------------------------------------------------------------------------------
  {
    paint_baseline(pixels);
    pixels.setDistinct((PIXEL_ADDR_TYPE)(NUM * 3 - 1), 0x42);  // led 29 -> pixel 9, channel b
    CHECK(pixels.getColor(NUM - 1) == 0x102042, "setDistinct(num*3-1) writes the last channel");

    paint_baseline(pixels);
    pixels.setDistinct((PIXEL_ADDR_TYPE)(NUM * 3), 0x99);  // first invalid led
    CHECK(count_pixels_with(pixels, BASELINE) == NUM, "setDistinct(num*3) is a no-op");
    pixels.setDistinct((PIXEL_ADDR_TYPE)-1, 0x99);  // max representable led
    CHECK(count_pixels_with(pixels, BASELINE) == NUM, "setDistinct(max led) is a no-op");
    CHECK(sentinels_intact(), "setDistinct probing: sentinels intact");
  }

  // -------------------------------------------------------------------------------------------
  // setPixelRGB(PRGB*) stays bounds-checked (the one setter that always was).
  // -------------------------------------------------------------------------------------------
  {
    paint_baseline(pixels);
    PRGB prgb;
    prgb.setColor(0xAA, 0xBB, 0xCC);
    prgb.pixel = 200;
    pixels.setPixelRGB(&prgb);
    CHECK(count_pixels_with(pixels, BASELINE) == NUM, "setPixelRGB(PRGB*) ignores pixel=200");
    prgb.pixel = 3;
    pixels.setPixelRGB(&prgb);
    CHECK(pixels.getColor(3) == 0xAABBCC, "setPixelRGB(PRGB*) writes pixel 3");
    CHECK(sentinels_intact(), "setPixelRGB(PRGB*): sentinels intact");
  }

  printf("%s: %d checks, %d failures\n", failures == 0 ? "PASS" : "FAIL", checks, failures);
  return failures == 0 ? 0 : 1;
}
