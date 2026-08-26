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

# MPR121, built on its own include set.
#
# Not folded into the generic rule: MPR121.cpp needs the Wire shim, and $(SOURCES) would relink the
# RS485 library into a test that has nothing to do with it -- the same reason PixelUtil is separate.
#
# DEBUG_LEVEL stays undefined, matching the RS485 suite. That is the release configuration, which is
# the one that ships; the DEBUG5 touch/release tracing in readTouchInputs() compiles out entirely.
MPR121_SOURCES  := $(ROOT)/MPR121/MPR121.cpp
MPR121_INCLUDES := -I shim -I $(ROOT)/MPR121 -I $(ROOT)/Debug
#
# Built TWICE, like PixelUtil and for the same reason: the two configurations differ by a -D that
# changes which code exists.
#
#   mpr121_test        -- MPR121.cpp self-defaults DEBUG_LEVEL to DEBUG_MID when the consumer sets
#                         none, so this is the configuration that actually ships for callers that
#                         say nothing. Its DEBUG5 touch/release tracing is compiled out.
#   mpr121_test_trace  -- DEBUG_LEVEL_MPR121=5, the only configuration in which that tracing exists.
#                         Without this binary the release-trace assertions are unreachable and a
#                         regression to the unreachable-branch form would go unnoticed: it was
#                         verified by mutation that reverting that fix fails ONLY here.
MPR121_TESTS    := mpr121_test mpr121_test_trace

# RFM95Socket, on its own include set and its own shims (LoRa.h, SPI.h).
#
# Debug.cpp IS linked here, unlike every other suite in this file. RFM95Socket.cpp self-defaults
# DEBUG_LEVEL to DEBUG_TRACE when the consumer sets none -- so the configuration that ships for a
# caller that says nothing is the one with tracing compiled IN, and its printSocketMsg() calls
# print_hex_buffer(), which lives in Debug.cpp. Stubbing that instead would have meant the tested
# build was not the shipped one.
RFM95_SOURCES  := $(ROOT)/RFM95Socket/RFM95Socket.cpp
#
# The HEADER is a prerequisite, not just the .cpp. Everything this suite pins hardest -- the pin
# map, the packed layout, the size ceilings -- lives in RFM95Socket.h as macros and structs, so a
# build that does not depend on it will happily re-run a stale binary after the header changed.
# Found by mutation: changing RFM95_SPARKFUN_1CH_RESET from -1 to SparkFun's 5 left the suite
# GREEN until a clean rebuild, which is the precise failure a test suite must not have.
RFM95_HEADERS  := $(ROOT)/RFM95Socket/RFM95Socket.h $(ROOT)/Socket/Socket.h
RFM95_INCLUDES := -I shim -I $(ROOT)/RFM95Socket -I $(ROOT)/Socket -I $(ROOT)/Debug
#
# Debug.cpp is compiled to its OWN object rather than added to RFM95_SOURCES, because its debug
# level has to be set independently of RFM95Socket.cpp's. Putting it on the same command line would
# mean one -DDEBUG_LEVEL for both, which collides with RFM95Socket.cpp's own
# `#ifdef DEBUG_LEVEL_RFM95SOCKET / #define DEBUG_LEVEL` (macro redefinition) and would make the
# quiet variant below impossible to express. Built at DEBUG_TRACE so print_hex_buffer() actually
# formats bytes -- at no level its body compiles away to an empty loop, and linking that would mean
# the trace build's frame-tracing was exercised only as far as the call instruction.
DEBUG_OBJ      := $(BUILD)/debug_trace.o
#
# Built TWICE, for the reason the suite exists:
#
#   rfm95_socket_test        -- consumer sets nothing, so DEBUG_LEVEL is DEBUG_TRACE. The shipping
#                               default, and the configuration in which the frame-tracing exists.
#   rfm95_socket_test_quiet  -- DEBUG_LEVEL_RFM95SOCKET=DEBUG_ERROR, tracing compiled out.
#
# The second binary is not symmetry, and this was checked rather than assumed. RFM69Socket's
# runt-frame length check sits inside `#if DEBUG_LEVEL == DEBUG_TRACE`, so it does not exist in the
# build that ships. Moving RFM95Socket's check into the same #if was tried as a mutation: the trace
# binary stayed GREEN (0 failures) and only the quiet binary caught it (3 failures). So a
# debug-gated safety check is invisible to a single-configuration suite, and this second build is
# the only thing standing between us and repeating that defect.
RFM95_TESTS    := rfm95_socket_test rfm95_socket_test_quiet

BINARIES := $(addprefix $(BUILD)/,$(TESTS) $(PIXEL_TESTS) $(MPR121_TESTS) $(RFM95_TESTS))

.PHONY: all test clean
all: test

test: $(BINARIES) rfm95-layout
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

$(BUILD)/mpr121_test: mpr121_test.cpp $(MPR121_SOURCES) shim/Arduino.h shim/Wire.h | $(BUILD)
	$(CXX) $(CXXFLAGS) $(MPR121_INCLUDES) -o $@ $< $(MPR121_SOURCES)

$(BUILD)/mpr121_test_trace: mpr121_test.cpp $(MPR121_SOURCES) shim/Arduino.h shim/Wire.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -DDEBUG_LEVEL_MPR121=5 $(MPR121_INCLUDES) -o $@ $< $(MPR121_SOURCES)

$(DEBUG_OBJ): $(ROOT)/Debug/Debug.cpp $(ROOT)/Debug/Debug.h shim/Arduino.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -DDEBUG_LEVEL=5 -I shim -I $(ROOT)/Debug -c -o $@ $<

$(BUILD)/rfm95_socket_test: rfm95_socket_test.cpp $(RFM95_SOURCES) $(RFM95_HEADERS) $(DEBUG_OBJ) shim/Arduino.h shim/LoRa.h shim/SPI.h | $(BUILD)
	$(CXX) $(CXXFLAGS) $(RFM95_INCLUDES) -o $@ $< $(RFM95_SOURCES) $(DEBUG_OBJ)

$(BUILD)/rfm95_socket_test_quiet: rfm95_socket_test.cpp $(RFM95_SOURCES) $(RFM95_HEADERS) $(DEBUG_OBJ) shim/Arduino.h shim/LoRa.h shim/SPI.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -DDEBUG_LEVEL_RFM95SOCKET=1 $(RFM95_INCLUDES) -o $@ $< $(RFM95_SOURCES) $(DEBUG_OBJ)

# Cross-ABI layout check: the header must have the SAME size and the SAME field offsets under the
# host ABI and under -fpack-struct=1, which is the closest host proxy for AVR's byte-aligned layout.
#
# This is what turns "packed so an AVR LoRa peer is possible later" from an intention into a checked
# claim. It is -fsyntax-only and compiles the header alone: the static_asserts in RFM95Socket.h are
# the assertion, so a layout that moves fails the BUILD rather than a runtime comparison.
#
# Deliberately NOT wired into HMTL/tests/layout/ -- there is no AVR LoRa peer yet, and that would be
# three edits across a submodule boundary (including the hardcoded GUARDED list in
# negative_control.py) to cover a node that does not exist.
.PHONY: rfm95-layout
rfm95-layout: $(RFM95_HEADERS)
	@echo "-- rfm95 header layout: host ABI --"
	$(CXX) -std=c++11 -Wall -Wextra -fsyntax-only $(RFM95_INCLUDES) -include Arduino.h -include Socket.h $(ROOT)/RFM95Socket/RFM95Socket.h
	@echo "-- rfm95 header layout: -fpack-struct=1 (AVR proxy) --"
	$(CXX) -std=c++11 -Wall -Wextra -fsyntax-only -fpack-struct=1 $(RFM95_INCLUDES) -include Arduino.h -include Socket.h $(ROOT)/RFM95Socket/RFM95Socket.h

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
