//
// rfm95_socket_test.cpp — host unit tests for RFM95Socket.
//
// Build/run:  make -C ArduinoLibs/test        (or `make test-libs` from the super-repo)
//
// NOT `make -C ArduinoLibs test`: there is no Makefile at the ArduinoLibs root, so that exits 0
// having run nothing -- a false SUCCESS indistinguishable from a passing suite.
//
// What this suite is for, in priority order:
//
//   (1) The PIN MAP. It is the one thing here that a bench cannot check usefully: a wrong CS, a
//       wrong SPI bus and a dead module all present identically as initialized() == false, so
//       flashing the board tells you something is wrong and never which. The netlist says
//       SCK 14 / MISO 12 / MOSI 13 / CS 16 / DIO0 26 and RESET NOT CONNECTED, and these tests pin
//       every one of those, including the negative that the ESP32 default VSPI pins (18/19/23/5)
//       never appear -- GPIO 23 is [env:esp32_lora_gw]'s PIXELS_CLOCK, so getting this wrong does
//       not just fail to talk to the radio, it drives the pixel clock line.
//
//   (2) The SOFTWARE ADDRESS FILTER. arduino-LoRa has no addressing at all; every node on the
//       frequency receives every packet, and getMsg()'s SOCKET_ADDRESS_MATCH is the only thing
//       that stops each of them acting on all of it. The half that matters is the NEGATIVE one --
//       "a packet for someone else is rejected" -- because the positive half passes just as well
//       on a socket that filters nothing.
//
//   (3) The RUNT-FRAME CHECK, which exists because RFM69Socket's equivalent is inside
//       `#if DEBUG_LEVEL == DEBUG_TRACE` and therefore absent from the build that ships. This
//       suite runs both configurations for exactly that reason -- see the Makefile.
//
// Style follows rs485_receive_test.cpp: one translation unit, a CHECK macro, no framework.
//
#include "Arduino.h"
#include "SPI.h"
#include "LoRa.h"
#include "Socket.h"
#include "RFM95Socket.h"

#include <stdio.h>
#include <string.h>
#include <vector>

// ---------------------------------------------------------------------------------------------
// Shim globals (declared extern in test/shim/*.h)
// ---------------------------------------------------------------------------------------------
unsigned long  test_millis_now = 0;
uint8_t        test_pin_state[64];
bool           test_malloc_should_fail = false;
std::string    test_debug_output;
HardwareSerial Serial;
SPIClass       SPI;
TestLoRaClass  LoRa;

// Socket ships no Socket.cpp, so its vtable and typeinfo have no home translation unit. Anchored
// here for the same reason rs485_receive_test.cpp anchors it: a host build with RTTI on references
// the base's typeinfo, and this also makes an accidental signature change a link error rather than
// something -fsyntax-only would miss.
void          Socket::setup() {}
boolean       Socket::initialized() { return false; }
byte         *Socket::initBuffer(byte *data, uint16_t) { return data; }
void          Socket::sendMsgTo(uint16_t, const byte *, const byte) {}
const byte   *Socket::getMsg(unsigned int *retlen) { *retlen = 0; return NULL; }
const byte   *Socket::getMsg(uint16_t, unsigned int *retlen) { *retlen = 0; return NULL; }
byte          Socket::getLength() { return 0; }
void         *Socket::headerFromData(const void *data) { return (void *)data; }
socket_addr_t Socket::sourceFromData(void *) { return 0; }
socket_addr_t Socket::destFromData(void *) { return 0; }

// ---------------------------------------------------------------------------------------------
// Harness
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

static const socket_addr_t MY_ADDR    = 0x0042;
static const socket_addr_t OTHER_ADDR = 0x0043;
static const socket_addr_t THIRD_ADDR = 0x0099;

// Build a frame exactly as a peer would put it on the air: 6-byte header then payload.
// Written byte-by-byte rather than by memcpy-ing the struct, so that if the struct's layout ever
// moves these tests disagree with it instead of moving along with it.
static std::vector<uint8_t> wire_frame(uint8_t id, socket_addr_t source, socket_addr_t dest,
                                       uint8_t flags, const uint8_t *payload, uint8_t len) {
  std::vector<uint8_t> f;
  f.push_back(id);
  f.push_back((uint8_t)(source & 0xFF));
  f.push_back((uint8_t)(source >> 8));
  f.push_back((uint8_t)(dest & 0xFF));
  f.push_back((uint8_t)(dest >> 8));
  f.push_back(flags);
  for (uint8_t i = 0; i < len; i++) f.push_back(payload[i]);
  return f;
}

// A socket that has been through init() + setup() with the radio answering.
static void fresh_socket(RFM95Socket &sock, socket_addr_t addr) {
  LoRa.test_reset();
  SPI.test_reset();
  sock.init(addr);
  sock.setup();
}

static const uint8_t PAYLOAD[] = { 0xDE, 0xAD, 0xBE, 0xEF };

int main(void) {
  printf("rfm95_socket_test\n");

  // -------------------------------------------------------------------------------------------
  // Header layout
  // -------------------------------------------------------------------------------------------
  // The static_asserts in the header already fail the BUILD if any of this moves; these runtime
  // checks exist so the failure is also legible at run time, and so the field order the wire_frame()
  // helper above assumes is stated in one more place than the struct itself.
  {
    CHECK(sizeof(rfm95_socket_hdr_t) == 6, "header is 6 bytes on the wire");
    CHECK(offsetof(rfm95_socket_hdr_t, ID) == 0, "ID at offset 0");
    CHECK(offsetof(rfm95_socket_hdr_t, source) == 1, "source at offset 1 -- no interior pad");
    CHECK(offsetof(rfm95_socket_hdr_t, address) == 3, "address at offset 3");
    CHECK(offsetof(rfm95_socket_hdr_t, flags) == 5, "flags at offset 5");
    CHECK(RFM95_BUFFER_TOTAL(10) == 16, "BUFFER_TOTAL adds the header");
    CHECK(RFM95_DATA_LENGTH(16) == 10, "DATA_LENGTH removes it");
  }

  // -------------------------------------------------------------------------------------------
  // Pin map -- the finding this library exists around
  // -------------------------------------------------------------------------------------------
  {
    RFM95Socket sock;
    fresh_socket(sock, MY_ADDR);

    CHECK(sock.initialized(), "a radio that answers leaves the socket initialized");

    CHECK(LoRa.set_pins_calls == 1, "setPins is called exactly once");
    CHECK(LoRa.ss_pin == 16, "CS is GPIO 16 (net RFM_CS: U1.IO16 <-> U2.NSS)");
    CHECK(LoRa.dio0_pin == 26, "DIO0 is GPIO 26 (net RFM_INT/26: U1.IO26 <-> U2.DIO0)");

    // The one that SparkFun's own examples get wrong in two different ways. RFM_RST reaches
    // U2.!RESET and a 10k pull-up to 3.3V and stops there; no GPIO is on that net. -1 is what
    // makes LoRa.cpp skip its reset sequence entirely.
    CHECK(LoRa.reset_pin == -1, "RESET is -1: the net never reaches the ESP32");
    CHECK(LoRa.reset_pin != 5 && LoRa.reset_pin != 27,
          "and specifically not SparkFun's 5 or 27, which are broken out to headers");

    CHECK(SPI.begin_calls == 1, "SPI is started explicitly, not left to LoRa.begin()'s default");
    CHECK(SPI.sck == 14, "SCK is GPIO 14");
    CHECK(SPI.miso == 12, "MISO is GPIO 12");
    CHECK(SPI.mosi == 13, "MOSI is GPIO 13");
    CHECK(SPI.ss == 16, "SPI SS matches the radio CS");

    // Negative control. If SPI were left at the ESP32 default (VSPI 18/19/23/5) the radio would be
    // silent AND GPIO 23 -- PIXELS_CLOCK in [env:esp32_lora_gw] -- would be driven by the SPI
    // peripheral. Asserting the positive alone would still pass if someone "fixed" a failure by
    // hardcoding the default pins.
    CHECK(SPI.sck != 18 && SPI.miso != 19 && SPI.mosi != 23,
          "and never the default VSPI pins, one of which is the pixel clock");

    CHECK(LoRa.frequency == RFM95_FREQ_US, "frequency is the 915 MHz US band");
    CHECK(LoRa.frequency == 915000000L, "which is 915 MHz stated numerically, not via the macro");
  }

  // Overridden pins are honoured -- the defaults are a convenience for one board, not a wiring law.
  {
    RFM95Socket sock;
    LoRa.test_reset();
    SPI.test_reset();
    sock.init(MY_ADDR, 433000000L);
    sock.setRadioPins(5, 6, 7);
    sock.setSPIPins(1, 2, 3);
    sock.setup();
    CHECK(LoRa.ss_pin == 5 && LoRa.reset_pin == 6 && LoRa.dio0_pin == 7, "radio pins are overridable");
    CHECK(SPI.sck == 1 && SPI.miso == 2 && SPI.mosi == 3, "SPI pins are overridable");
    CHECK(LoRa.frequency == 433000000L, "frequency is overridable");
  }

  // -------------------------------------------------------------------------------------------
  // A radio that does not answer
  // -------------------------------------------------------------------------------------------
  // begin() returning 0 is the real-world "wrong CS / wrong bus / dead module" case. What matters
  // is not just that initialized() is false, but that everything downstream then does NOTHING --
  // a socket that keeps calling into a driver that never came up is how a bring-up failure turns
  // into a hang instead of a clean false.
  {
    RFM95Socket sock;
    LoRa.test_reset();
    SPI.test_reset();
    LoRa.begin_result = 0;
    sock.init(MY_ADDR);
    sock.setup();

    CHECK(!sock.initialized(), "a radio that does not answer leaves the socket uninitialized");

    byte buf[64];
    byte *data = sock.initBuffer(buf, sizeof(buf));
    memcpy(data, PAYLOAD, sizeof(PAYLOAD));
    sock.sendMsgTo(OTHER_ADDR, data, sizeof(PAYLOAD));
    CHECK(LoRa.tx_packets.empty(), "and sendMsgTo transmits nothing");

    std::vector<uint8_t> f = wire_frame(1, OTHER_ADDR, MY_ADDR, 0, PAYLOAD, sizeof(PAYLOAD));
    LoRa.feed(f.data(), f.size());
    unsigned int retlen = 99;
    CHECK(sock.getMsg(MY_ADDR, &retlen) == NULL, "and getMsg delivers nothing");
    CHECK(retlen == 0, "and clears retlen even on the uninitialized path");
  }

  // -------------------------------------------------------------------------------------------
  // Transmit
  // -------------------------------------------------------------------------------------------
  {
    RFM95Socket sock;
    fresh_socket(sock, MY_ADDR);

    byte buf[64];
    byte *data = sock.initBuffer(buf, sizeof(buf));
    CHECK(data == buf + 6, "initBuffer reserves the header");
    CHECK(sock.send_data_size == 64 - 6, "and reports the usable size");

    memcpy(data, PAYLOAD, sizeof(PAYLOAD));
    sock.sendMsgTo(OTHER_ADDR, data, sizeof(PAYLOAD));

    CHECK(LoRa.tx_packets.size() == 1, "one packet is transmitted");
    if (LoRa.tx_packets.size() == 1) {
      const std::vector<uint8_t> &tx = LoRa.tx_packets[0];
      CHECK(tx.size() == 6 + sizeof(PAYLOAD), "sized header + payload");
      CHECK(tx[0] == 0, "first message carries ID 0");
      CHECK(tx[1] == (MY_ADDR & 0xFF) && tx[2] == (MY_ADDR >> 8), "source is our address, little-endian");
      CHECK(tx[3] == (OTHER_ADDR & 0xFF) && tx[4] == (OTHER_ADDR >> 8), "destination as given");
      CHECK(tx[5] == 0, "flags zero");
      CHECK(memcmp(&tx[6], PAYLOAD, sizeof(PAYLOAD)) == 0, "payload intact after the header");
    }

    // The ID must advance, or duplicate suppression built on top of it later can never work.
    sock.sendMsgTo(OTHER_ADDR, data, sizeof(PAYLOAD));
    CHECK(LoRa.tx_packets.size() == 2 && LoRa.tx_packets[1][0] == 1, "the message ID increments");

    // Broadcast goes out as SOCKET_ADDR_ANY itself. RFM69Socket has to convert, because its driver
    // owns a broadcast address; LoRa has no addressing to convert to.
    sock.sendMsgTo(SOCKET_ADDR_ANY, data, sizeof(PAYLOAD));
    const std::vector<uint8_t> &bc = LoRa.tx_packets[2];
    CHECK(bc[3] == 0xFF && bc[4] == 0xFF, "broadcast travels as SOCKET_ADDR_ANY, unconverted");
  }

  // A send that collides with an in-flight transmit is dropped, not queued or half-written.
  {
    RFM95Socket sock;
    fresh_socket(sock, MY_ADDR);
    byte buf[64];
    byte *data = sock.initBuffer(buf, sizeof(buf));
    memcpy(data, PAYLOAD, sizeof(PAYLOAD));

    LoRa.begin_packet_result = 0;
    sock.sendMsgTo(OTHER_ADDR, data, sizeof(PAYLOAD));
    CHECK(LoRa.tx_packets.empty(), "a busy radio drops the send rather than writing a partial frame");

    LoRa.begin_packet_result = 1;
    sock.sendMsgTo(OTHER_ADDR, data, sizeof(PAYLOAD));
    CHECK(LoRa.tx_packets.size() == 1, "and the next send still works -- one packet lost, not the link");
  }

  // -------------------------------------------------------------------------------------------
  // Receive: the address filter
  // -------------------------------------------------------------------------------------------
  {
    RFM95Socket sock;
    fresh_socket(sock, MY_ADDR);

    // Addressed to us.
    std::vector<uint8_t> mine = wire_frame(7, OTHER_ADDR, MY_ADDR, 0, PAYLOAD, sizeof(PAYLOAD));
    LoRa.feed(mine.data(), mine.size());
    unsigned int retlen = 0;
    const byte *got = sock.getMsg(MY_ADDR, &retlen);
    CHECK(got != NULL, "a packet addressed to us is delivered");
    CHECK(retlen == sizeof(PAYLOAD), "with the payload length, header excluded");
    CHECK(got && memcmp(got, PAYLOAD, sizeof(PAYLOAD)) == 0, "and the payload intact");
    CHECK(sock.getLength() == 6 + sizeof(PAYLOAD), "getLength reports the whole frame");
    CHECK(sock.sourceFromData((void *)got) == OTHER_ADDR, "sourceFromData reads back the sender");
    CHECK(sock.destFromData((void *)got) == MY_ADDR, "destFromData reads back the destination");

    // Broadcast.
    std::vector<uint8_t> bcast = wire_frame(8, OTHER_ADDR, SOCKET_ADDR_ANY, 0, PAYLOAD, sizeof(PAYLOAD));
    LoRa.feed(bcast.data(), bcast.size());
    retlen = 0;
    CHECK(sock.getMsg(MY_ADDR, &retlen) != NULL, "a broadcast packet is delivered");
    CHECK(retlen == sizeof(PAYLOAD), "with its payload length");

    // THE NEGATIVE HALF. Every node on the frequency received this frame; only the filter stops
    // this one acting on it. Without this check a socket that ignored hdr.address entirely would
    // pass every other receive test in this file.
    std::vector<uint8_t> theirs = wire_frame(9, OTHER_ADDR, THIRD_ADDR, 0, PAYLOAD, sizeof(PAYLOAD));
    LoRa.feed(theirs.data(), theirs.size());
    retlen = 99;
    CHECK(sock.getMsg(MY_ADDR, &retlen) == NULL, "a packet for a third party is REJECTED");
    CHECK(retlen == 0, "and retlen is cleared on rejection");

    // getLength() stays non-zero after a filter rejection, and this is load-bearing rather than
    // incidental: it is the ONLY way a caller can tell "a frame arrived and was not for me" from
    // "no frame arrived". The RFM95_SocketTest example counts filter rejections with exactly this
    // test, and a bench run's rejected-count is the only evidence that addressing works at all --
    // so if this ever became 0 the bench would silently lose its one meaningful number.
    CHECK(sock.getLength() == 6 + sizeof(PAYLOAD),
          "getLength survives a filter rejection, so a rejected frame is distinguishable from none");

    // ...and rejecting it does not wedge the receiver.
    LoRa.feed(mine.data(), mine.size());
    retlen = 0;
    CHECK(sock.getMsg(MY_ADDR, &retlen) != NULL, "and the next packet for us still arrives");
  }

  // A socket listening on SOCKET_ADDR_ANY takes everything -- the promiscuous case a bridge needs.
  {
    RFM95Socket sock;
    fresh_socket(sock, MY_ADDR);
    std::vector<uint8_t> theirs = wire_frame(1, OTHER_ADDR, THIRD_ADDR, 0, PAYLOAD, sizeof(PAYLOAD));
    LoRa.feed(theirs.data(), theirs.size());
    unsigned int retlen = 0;
    CHECK(sock.getMsg(SOCKET_ADDR_ANY, &retlen) != NULL,
          "listening on SOCKET_ADDR_ANY accepts a packet addressed elsewhere");
  }

  // getMsg() with no address argument uses our own -- and must not accidentally become promiscuous.
  {
    RFM95Socket sock;
    fresh_socket(sock, MY_ADDR);
    std::vector<uint8_t> theirs = wire_frame(1, OTHER_ADDR, THIRD_ADDR, 0, PAYLOAD, sizeof(PAYLOAD));
    LoRa.feed(theirs.data(), theirs.size());
    unsigned int retlen = 99;
    CHECK(sock.getMsg(&retlen) == NULL, "the no-address getMsg still filters on our own address");
    CHECK(retlen == 0, "and clears retlen");

    std::vector<uint8_t> mine = wire_frame(2, OTHER_ADDR, MY_ADDR, 0, PAYLOAD, sizeof(PAYLOAD));
    LoRa.feed(mine.data(), mine.size());
    retlen = 0;
    CHECK(sock.getMsg(&retlen) != NULL, "and delivers one that is for us");
  }

  // -------------------------------------------------------------------------------------------
  // Receive: malformed frames
  // -------------------------------------------------------------------------------------------
  // These run identically in BOTH built configurations, which is the point -- RFM69Socket's runt
  // check is inside `#if DEBUG_LEVEL == DEBUG_TRACE` and so does not exist in a release build.
  {
    RFM95Socket sock;
    fresh_socket(sock, MY_ADDR);

    // Shorter than the header. Cast-then-read here is a read past the end of the frame.
    const uint8_t runt[] = { 1, 2, 3 };
    LoRa.feed(runt, sizeof(runt));
    unsigned int retlen = 99;
    CHECK(sock.getMsg(MY_ADDR, &retlen) == NULL, "a frame shorter than the header is rejected");
    CHECK(retlen == 0, "and retlen cleared");
    CHECK(sock.getLength() == 0, "and getLength does not report a frame that was never accepted");

    // Exactly the header, no payload: legal, and a zero-length payload is a real thing to send.
    std::vector<uint8_t> bare = wire_frame(3, OTHER_ADDR, MY_ADDR, 0, NULL, 0);
    LoRa.feed(bare.data(), bare.size());
    retlen = 99;
    CHECK(sock.getMsg(MY_ADDR, &retlen) != NULL, "a header-only frame is accepted");
    CHECK(retlen == 0, "with a zero-length payload");

    // Announced longer than delivered. parsePacket() reporting more than the stream can supply is
    // exactly the shape where trusting the announced length reads uninitialised buffer as payload.
    std::vector<uint8_t> full = wire_frame(4, OTHER_ADDR, MY_ADDR, 0, PAYLOAD, sizeof(PAYLOAD));
    LoRa.feed_truncated(full.data(), 7, (int)full.size());
    retlen = 99;
    CHECK(sock.getMsg(MY_ADDR, &retlen) == NULL, "a frame that announces more than it delivers is rejected");
    CHECK(retlen == 0, "and retlen cleared");

    // Recovery after all of that.
    LoRa.feed(full.data(), full.size());
    retlen = 0;
    CHECK(sock.getMsg(MY_ADDR, &retlen) != NULL, "and a good frame after the bad ones still arrives");
    CHECK(retlen == sizeof(PAYLOAD), "with the right length");
  }

  // An empty radio is not an error, just nothing to do.
  {
    RFM95Socket sock;
    fresh_socket(sock, MY_ADDR);
    unsigned int retlen = 99;
    CHECK(sock.getMsg(MY_ADDR, &retlen) == NULL, "no packet waiting returns NULL");
    CHECK(retlen == 0, "and retlen is cleared");
  }

  // -------------------------------------------------------------------------------------------
  // Maximum-size frame
  // -------------------------------------------------------------------------------------------
  // 249 payload bytes + 6 header = 255, the SX127x explicit-header maximum. Socket pins a payload
  // length to one byte in the base class, so this is the largest frame the abstraction can express
  // at all -- worth pinning, because "widen it for LoRa's larger MTU" is an easy thing to attempt
  // and there is no field to widen.
  {
    RFM95Socket sock;
    fresh_socket(sock, MY_ADDR);
    CHECK(RFM95_MAX_DATA_LEN == 249, "the usable payload maximum is 249 bytes");

    std::vector<uint8_t> big(RFM95_MAX_DATA_LEN);
    for (size_t i = 0; i < big.size(); i++) big[i] = (uint8_t)(i & 0xFF);
    std::vector<uint8_t> f = wire_frame(5, OTHER_ADDR, MY_ADDR, 0, big.data(), (uint8_t)big.size());
    CHECK(f.size() == 255, "a full frame is exactly 255 bytes");

    LoRa.feed(f.data(), f.size());
    unsigned int retlen = 0;
    const byte *got = sock.getMsg(MY_ADDR, &retlen);
    CHECK(got != NULL, "a maximum-size frame is delivered");
    CHECK(retlen == RFM95_MAX_DATA_LEN, "with its full payload length");
    CHECK(got && memcmp(got, big.data(), big.size()) == 0, "and its bytes intact");
  }

  // -------------------------------------------------------------------------------------------
  // Oversized TRANSMIT -- the half the max-size block above did not cover, and a real defect
  // -------------------------------------------------------------------------------------------
  // Found in post-PR self-review. Everything above tested the maximum-size RECEIVE path; nothing
  // tested transmit, and transmit is where the arithmetic breaks:
  //
  //   RFM95_BUFFER_TOTAL() casts to uint8_t, `datalength` is a byte because Socket pins it to one,
  //   so 250 gives a total of 0 and 255 gives 5. Before the bounds check, sendMsgTo(..., 250) put an
  //   EMPTY frame on the air and sendMsgTo(..., 255) put a 5-byte RUNT there -- the exact shape
  //   getMsg()'s unconditional length check exists to reject. The library was manufacturing the
  //   frames its own receive path defends against, and reporting success.
  //
  // RFM69Socket cannot reach this (61-byte ceiling). It is reachable here only because
  // RFM95_MAX_PACKET is exactly 255.
  {
    RFM95Socket sock;
    fresh_socket(sock, MY_ADDR);
    byte buf[300];
    byte *data = sock.initBuffer(buf, 262);
    memset(data, 0xAA, 255);

    // The boundary itself must still work.
    sock.sendMsgTo(OTHER_ADDR, data, RFM95_MAX_DATA_LEN);
    CHECK(LoRa.tx_packets.size() == 1 && LoRa.tx_packets[0].size() == 255,
          "249 bytes -- the maximum -- still transmits as a full 255-byte frame");

    // 250: total wraps to 0.
    LoRa.tx_packets.clear();
    sock.sendMsgTo(OTHER_ADDR, data, 250);
    CHECK(LoRa.tx_packets.empty(), "250 bytes is dropped, not sent as an empty frame");

    // 255: total wraps to 5, which is shorter than the header.
    LoRa.tx_packets.clear();
    sock.sendMsgTo(OTHER_ADDR, data, 255);
    CHECK(LoRa.tx_packets.empty(), "255 bytes is dropped, not sent as a 5-byte runt");

    // A drop must cost one message, not the link.
    LoRa.tx_packets.clear();
    sock.sendMsgTo(OTHER_ADDR, data, 4);
    CHECK(LoRa.tx_packets.size() == 1 && LoRa.tx_packets[0].size() == 10,
          "and a normal send straight after an oversized one still works");
  }

  // initBuffer with a buffer smaller than the header: the subtraction underflows uint16_t, so a
  // caller passing 0 was told 65530 bytes were available and handed a pointer past its own array.
  {
    RFM95Socket sock;
    fresh_socket(sock, MY_ADDR);
    byte tiny[4];
    byte *d = sock.initBuffer(tiny, sizeof(tiny));
    CHECK(sock.send_data_size == 0, "a sub-header buffer reports zero usable bytes, not 65530");
    CHECK(d == tiny, "and does not hand back a pointer past the end of it");
  }

  printf("\n%d checks, %d failures\n", checks, failures);
  printf(failures ? "SOME TESTS FAILED\n" : "ALL TESTS PASSED\n");
  return failures ? 1 : 0;
}
