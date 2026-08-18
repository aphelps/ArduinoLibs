# Host unit tests for ArduinoLibs. No hardware, no Arduino toolchain — a plain host C++ compiler and
# the Arduino.h / FastLED.h shims in shim/.
#
#   make            build and run everything
#   make clean
#
# From the super-repo, `make test-libs` (or plain `make test`) runs this.
#
# NB `make -C ArduinoLibs test` does NOT work: there is no Makefile at the ArduinoLibs root, so it
# prints "Nothing to be done for `test'" and exits 0 -- a false SUCCESS that looks like a passing
# suite. Use `make -C ArduinoLibs/test` if you want to invoke it by path.
#
# -DRS485_HARDWARE_SERIAL is REQUIRED, not optional: RS485Utils.h sets RS485UTILS_SUPPORTED to 1 only
# for `defined(RS485_HARDWARE_SERIAL)` or `defined(__AVR__)`, and otherwise #if-s the entire library
# away — header and .cpp both. Without it this suite would compile an empty translation unit, link, and
# report success while testing nothing.
#
# DEBUG_LEVEL is deliberately left UNDEFINED. That is the release configuration, and it is the
# configuration in which getMsg()'s length checks used to vanish — so testing with debug enabled would
# miss the very defect the suite is here to pin.
#
# -std=c++11 is load-bearing for pixelutil_test, not just a default: that test replaces global
# operator new[]/delete[], and sized deallocation (which would need operator delete[](void*, size_t)
# replaced as well) only becomes the default from C++14. Raise the standard and you must add that
# overload, or the default sized delete will free() an interior pointer.

CXX      ?= c++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -O1 -g
ROOT     := ..
INCLUDES := -I shim -I $(ROOT)/RS485Utils -I $(ROOT)/RS485_non_blocking -I $(ROOT)/Socket -I $(ROOT)/Debug
DEFINES  := -DRS485_HARDWARE_SERIAL=Serial

BUILD    := build

# The RS485 suite's sources. PixelUtil.cpp is deliberately NOT here: it is compiled per-test below
# instead. Adding it to SOURCES would relink rs485_receive_test against it, dragging the FastLED
# shim and a PIXELS_* flag into a test that has nothing to do with pixels.
SOURCES  := $(ROOT)/RS485Utils/RS485Utils.cpp $(ROOT)/RS485_non_blocking/RS485_non_blocking.cpp
TESTS    := rs485_receive_test

# PixelUtil, built twice from one source.
#
# The pattern rule below cannot express this: it produces one binary per .cpp with one global set of
# DEFINES, and the two PixelUtil builds differ by -DBIG_PIXELS, which selects PIXEL_ADDR_TYPE in the
# header that BOTH the test and PixelUtil.cpp include. The widths are not cosmetic -- the range
# underflow this pins wraps to a different length in each (66 vs 65346), and only the uint16_t one is
# catastrophic -- so both are built and both are run.
#
# -DPIXELS_WS2812B_3 is explicit so PixelUtil_config.h does not fall through to its "No pixel
# definition compiler flag was specified" #warning on every build. It selects the single-wire
# addLeds<CHIPSET, DATA, ORDER> form, which is the only one the shim then instantiates.
PIXEL_SOURCES  := $(ROOT)/PixelUtil/PixelUtil.cpp
PIXEL_INCLUDES := -I shim -I $(ROOT)/PixelUtil -I $(ROOT)/Debug
PIXEL_DEFINES  := -DPIXELS_WS2812B_3
PIXEL_TESTS    := pixelutil_test pixelutil_test_big

BINARIES := $(addprefix $(BUILD)/,$(TESTS) $(PIXEL_TESTS))

.PHONY: all test clean
all: test

test: $(BINARIES)
	@rc=0; for t in $(BINARIES); do \
	  echo "-- $$(basename $$t) --"; \
	  $$t || rc=1; \
	done; \
	exit $$rc

$(BUILD)/%: %.cpp $(SOURCES) shim/Arduino.h | $(BUILD)
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) -o $@ $< $(SOURCES)

# Explicit targets, not the pattern rule: same source, two different -D sets.
$(BUILD)/pixelutil_test: pixelutil_test.cpp $(PIXEL_SOURCES) shim/Arduino.h shim/FastLED.h | $(BUILD)
	$(CXX) $(CXXFLAGS) $(PIXEL_DEFINES) $(PIXEL_INCLUDES) -o $@ $< $(PIXEL_SOURCES)

$(BUILD)/pixelutil_test_big: pixelutil_test.cpp $(PIXEL_SOURCES) shim/Arduino.h shim/FastLED.h | $(BUILD)
	$(CXX) $(CXXFLAGS) $(PIXEL_DEFINES) -DBIG_PIXELS $(PIXEL_INCLUDES) -o $@ $< $(PIXEL_SOURCES)

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
