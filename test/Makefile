# Host unit tests for ArduinoLibs. No hardware, no Arduino toolchain — a plain host C++ compiler and
# the Arduino.h shim in shim/.
#
#   make            build and run everything
#   make clean
#
# From the super-repo you can also use `make -C ArduinoLibs test`.
#
# -DRS485_HARDWARE_SERIAL is REQUIRED, not optional: RS485Utils.h sets RS485UTILS_SUPPORTED to 1 only
# for `defined(RS485_HARDWARE_SERIAL)` or `defined(__AVR__)`, and otherwise #if-s the entire library
# away — header and .cpp both. Without it this suite would compile an empty translation unit, link, and
# report success while testing nothing.
#
# DEBUG_LEVEL is deliberately left UNDEFINED. That is the release configuration, and it is the
# configuration in which getMsg()'s length checks used to vanish — so testing with debug enabled would
# miss the very defect the suite is here to pin.

CXX      ?= c++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -O1 -g
ROOT     := ..
INCLUDES := -I shim -I $(ROOT)/RS485Utils -I $(ROOT)/RS485_non_blocking -I $(ROOT)/Socket -I $(ROOT)/Debug
DEFINES  := -DRS485_HARDWARE_SERIAL=Serial

BUILD    := build
SOURCES  := $(ROOT)/RS485Utils/RS485Utils.cpp $(ROOT)/RS485_non_blocking/RS485_non_blocking.cpp
TESTS    := rs485_receive_test
BINARIES := $(addprefix $(BUILD)/,$(TESTS))

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

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
