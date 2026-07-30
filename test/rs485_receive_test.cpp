//
// rs485_receive_test.cpp — host unit tests for the RS485 receive path.
//
// Build/run:  make -C ArduinoLibs/test        (or `make test-libs` from the super-repo)
//
// NOT `make -C ArduinoLibs test`: there is no Makefile at the ArduinoLibs root, so that exits 0
// having run nothing -- a false SUCCESS indistinguishable from a passing suite.
//
// Why this exists: every defect covered here is a *negative* — "the bad thing no longer happens" —
// and three of the four are pure state-machine behaviour reachable only through specific byte
// sequences on the wire. A bench session cannot demonstrate them ("we ran it and nothing bad
// happened" is not evidence about a path that fires on a dropped STX), but a host test can, exactly
// and every run. This is the first test infrastructure in ArduinoLibs; the style deliberately matches
// WLED/usermods/rs485_bridge/tests/rs485_bridge_test.cpp — one translation unit, a CHECK macro, no
// framework.
//
// The four defects, all found auditing the vendored RS485_non_blocking after the ArduinoLibs#5
// self-review turned out to have verified byte-identity and packaging but never the logic:
//
//   (1) stale-packet re-delivery — a completed packet is never reset, so the next valid-form byte is
//       tested as a CRC over the PREVIOUS packet's buffer and can re-deliver it
//   (2) begin() does not check malloc — a receiver silently dead for a whole boot
//   (3) no timeout — a truncated frame leaves haveSTX_ set indefinitely
//   (4) getMsg()'s length checks live inside #if DEBUG_LEVEL >= DEBUG_TRACE, so release builds hand
//       out a sender-declared length with nothing validating it
//
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <vector>

#include "RS485Utils.h"

// ---------------------------------------------------------------------------------------------
// Shim globals (declared extern in test/shim/Arduino.h)
// ---------------------------------------------------------------------------------------------
unsigned long test_millis_now = 0;
uint8_t       test_pin_state[64];
bool          test_malloc_should_fail = false;
HardwareSerial Serial;

// Socket declares ten pure-in-spirit virtuals but ships no Socket.cpp, so its vtable and typeinfo
// have no home translation unit. The firmware never notices: Arduino builds with -fno-rtti, and
// RS485Socket overrides every one of them. A host build with RTTI on does notice, because
// RS485Socket's typeinfo references its base's. Rather than compile with -fno-rtti and hide it,
// anchor the base here -- it also documents a real fragility: give RS485Socket a signature that
// stops overriding one of these (e.g. changing setup() to return bool) and the link breaks with an
// undefined vtable reference that -fsyntax-only will never catch.
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

// Gammon's CRC-8: poly 0x8C, LSB-first, init 0. Transcribed from RS485_non_blocking.cpp:69-85 —
// this must stay a byte-for-byte match or the frames below are not the frames the library expects.
static uint8_t wire_crc8(const uint8_t *addr, uint8_t len) {
  uint8_t crc = 0;
  while (len--) {
    uint8_t inbyte = *addr++;
    for (uint8_t i = 8; i; i--) {
      uint8_t mix = (uint8_t)((crc ^ inbyte) & 0x01);
      crc >>= 1;
      if (mix) crc ^= 0x8C;
      inbyte >>= 1;
    }
  }
  return crc;
}

// One byte as Gammon puts it on the wire: each nibble sent as (nibble << 4) | (nibble ^ 0x0F).
// Only 16 of the 256 possible byte values pass the receiver's form check, which is why random line
// noise is a far weaker trigger for defect (1) than a dropped STX on a legitimate frame.
static void wire_push_byte(std::vector<uint8_t> &out, uint8_t what) {
  uint8_t hi = (uint8_t)(what >> 4);
  uint8_t lo = (uint8_t)(what & 0x0F);
  out.push_back((uint8_t)((hi << 4) | (hi ^ 0x0F)));
  out.push_back((uint8_t)((lo << 4) | (lo ^ 0x0F)));
}

static const uint8_t WIRE_STX = 2;
static const uint8_t WIRE_ETX = 3;

// Encode a complete framed packet: STX, the payload nibble-complemented, ETX, then the CRC.
static std::vector<uint8_t> wire_frame(const uint8_t *payload, uint8_t len) {
  std::vector<uint8_t> out;
  out.push_back(WIRE_STX);
  for (uint8_t i = 0; i < len; i++) wire_push_byte(out, payload[i]);
  out.push_back(WIRE_ETX);
  wire_push_byte(out, wire_crc8(payload, len));
  return out;
}

// Build a socket-layer message: [rs485_socket_hdr_t][payload]. `declaredLen` defaults to the real
// payload length; a test can lie about it to exercise the bounds checks of defect (4).
static std::vector<uint8_t> socket_msg(uint16_t src, uint16_t dst, const uint8_t *payload,
                                       uint8_t payloadLen, int declaredLen = -1) {
  rs485_socket_hdr_t hdr;
  memset(&hdr, 0, sizeof(hdr));
  hdr.ID      = 1;
  hdr.length  = (declaredLen < 0) ? payloadLen : (uint8_t)declaredLen;
  hdr.source  = src;
  hdr.address = dst;
  hdr.flags   = 0;
  std::vector<uint8_t> out((uint8_t *)&hdr, (uint8_t *)&hdr + sizeof(hdr));
  for (uint8_t i = 0; i < payloadLen; i++) out.push_back(payload[i]);
  return out;
}

// A socket ready to receive, with the serial queue drained.
static void fresh_socket(RS485Socket &sock, uint16_t myAddr) {
  Serial.clear();
  test_time_set(1000);
  sock.init(&Serial, 5 /* enable pin */, myAddr, RS485_RECV_BUFFER, false);
  sock.setup();
}

int main() {
  const uint16_t MY_ADDR    = 3;
  const uint16_t OTHER_ADDR = 9;
  const uint8_t  PAYLOAD[]  = { 0xFC, 0x00, 0x02, 0x0B, 0x01, 0x00, 0x03, 0x00, 0xAA, 0xBB };

  // 1) A well-formed packet is delivered exactly once, and getLength() survives the call.
  //
  // The getLength() half is not incidental: it guards the deferred-reset design against the
  // tempting-but-broken fix. Resetting the channel inside getMsg() *before* it returns would zero
  // inputPos_, and the WLED bridge reads rs485.getLength() AFTER getMsg() returns in order to
  // bounds-check the frame — so a reset-too-early makes every frame look impossibly short and the
  // bridge goes deaf while reporting itself healthy.
  {
    RS485Socket sock;
    fresh_socket(sock, MY_ADDR);
    std::vector<uint8_t> msg = socket_msg(OTHER_ADDR, MY_ADDR, PAYLOAD, sizeof(PAYLOAD));
    std::vector<uint8_t> framed = wire_frame(msg.data(), (uint8_t)msg.size());
    Serial.feed(framed.data(), framed.size());

    unsigned int retlen = 0;
    const byte *data = sock.getMsg(MY_ADDR, &retlen);
    CHECK(data != NULL, "good packet is delivered");
    CHECK(retlen == sizeof(PAYLOAD), "delivered payload length matches the declared length");
    CHECK(data != NULL && memcmp(data, PAYLOAD, sizeof(PAYLOAD)) == 0, "payload bytes round-trip");
    CHECK(sock.getLength() == (byte)msg.size(),
          "getLength() still reports the whole frame AFTER getMsg() returned");
  }

  // 2) DEFECT (1): a completed packet must not be re-delivered.
  //
  // The trigger is deterministic, not a coin flip. On a good CRC the library sets available_ and
  // returns without calling reset(), leaving haveSTX_/haveETX_/inputPos_ live. crc8(data_, inputPos_)
  // is therefore still the CRC byte the sender just transmitted — a value we know — so feeding that
  // one byte back, nibble-complemented, re-fires `return true` every single time.
  //
  // Against the unfixed library this test FAILS 100% of the time, which is what makes it a
  // regression test rather than a probability argument. (The original bug note said "roughly 1 in
  // 256 on line noise"; that is wrong twice over. Only 16 of 256 byte values pass the form check and
  // any that fails calls reset(), so uniform noise is ~1/65536 per byte-pair position. The realistic
  // trigger is a lost STX on the *next* legitimate frame: every byte of it is valid-form, so each of
  // its byte pairs gets an independent 1/256 shot — call it 1-in-4 to 1-in-10 per dropped STX.)
  {
    RS485Socket sock;
    fresh_socket(sock, MY_ADDR);
    std::vector<uint8_t> msg = socket_msg(OTHER_ADDR, MY_ADDR, PAYLOAD, sizeof(PAYLOAD));
    std::vector<uint8_t> framed = wire_frame(msg.data(), (uint8_t)msg.size());
    Serial.feed(framed.data(), framed.size());

    unsigned int retlen = 0;
    CHECK(sock.getMsg(MY_ADDR, &retlen) != NULL, "first delivery succeeds");

    // Now the stale-CRC byte, with no intervening STX.
    std::vector<uint8_t> staleTrigger;
    wire_push_byte(staleTrigger, wire_crc8(msg.data(), (uint8_t)msg.size()));
    Serial.feed(staleTrigger.data(), staleTrigger.size());

    retlen = 0;
    const byte *again = sock.getMsg(MY_ADDR, &retlen);
    CHECK(again == NULL, "DEFECT 1: stale packet must not be re-delivered");
    CHECK(retlen == 0, "DEFECT 1: retlen must be cleared when nothing was delivered");
  }

  // 3) DEFECT (1a): the same, when the frame was addressed to somebody else.
  //
  // getMsg() only hands back a payload when SOCKET_ADDRESS_MATCH passes; the mismatch path falls
  // through to `return NULL` having already consumed the packet via update(). So a fix that arms its
  // "packet consumed" flag only on the delivering path does nothing for address-filtering consumers
  // — and those are the majority: the WLED bridge listens promiscuously (RS485_ADDR_ANY, always
  // matches), but HMTL_Module and HMTL_Command_CLI pass their own address, so on a shared bus
  // mismatch is the COMMON case and the stale window would never close for them.
  {
    RS485Socket sock;
    fresh_socket(sock, MY_ADDR);
    std::vector<uint8_t> msg = socket_msg(OTHER_ADDR, 42 /* not us */, PAYLOAD, sizeof(PAYLOAD));
    std::vector<uint8_t> framed = wire_frame(msg.data(), (uint8_t)msg.size());
    Serial.feed(framed.data(), framed.size());

    unsigned int retlen = 0;
    CHECK(sock.getMsg(MY_ADDR, &retlen) == NULL, "frame for another node is not delivered to us");

    std::vector<uint8_t> staleTrigger;
    wire_push_byte(staleTrigger, wire_crc8(msg.data(), (uint8_t)msg.size()));
    Serial.feed(staleTrigger.data(), staleTrigger.size());

    /*
     * Poll with RS485_ADDR_ANY, not MY_ADDR. This matters, and the first version of this test got it
     * wrong: filtering on MY_ADDR again would return NULL whether or not the stale packet was
     * re-parsed, because the frame is addressed elsewhere and the address check discards it either
     * way — so the test passed even with the fix disabled, proving nothing. Polling promiscuously
     * makes a re-delivery observable: if the stale packet re-fires, ADDR_ANY matches it and we get a
     * bogus second delivery of a frame that arrived once.
     */
    retlen = 0;
    CHECK(sock.getMsg(RS485_ADDR_ANY, &retlen) == NULL,
          "DEFECT 1a: no re-delivery after an address-mismatch consumed the packet");
    CHECK(retlen == 0, "DEFECT 1a: retlen cleared, nothing re-delivered");
  }

  // 4) DEFECT (1), belt and braces: valid-form noise with no STX delivers nothing.
  {
    RS485Socket sock;
    fresh_socket(sock, MY_ADDR);
    std::vector<uint8_t> msg = socket_msg(OTHER_ADDR, MY_ADDR, PAYLOAD, sizeof(PAYLOAD));
    std::vector<uint8_t> framed = wire_frame(msg.data(), (uint8_t)msg.size());
    Serial.feed(framed.data(), framed.size());
    unsigned int retlen = 0;
    CHECK(sock.getMsg(MY_ADDR, &retlen) != NULL, "first delivery succeeds");

    /*
     * 256 valid-form byte pairs, none of them an STX -- starting AT the stale CRC value.
     *
     * That starting point is the whole point. An earlier version started at 0x00, and the first
     * completed byte then mismatched the stale CRC, which makes the unfixed library call reset() and
     * close the window by itself: the test passed with the fix disabled and proved nothing. Beginning
     * at the value that DOES match means the unfixed code re-delivers on the very first pair.
     */
    const uint8_t staleCrc = wire_crc8(msg.data(), (uint8_t)msg.size());
    std::vector<uint8_t> noise;
    for (int i = 0; i < 256; i++) wire_push_byte(noise, (uint8_t)((staleCrc + i) & 0xFF));
    Serial.feed(noise.data(), noise.size());

    int deliveries = 0;
    for (int i = 0; i < 300; i++) {
      retlen = 0;
      if (sock.getMsg(MY_ADDR, &retlen) != NULL) deliveries++;
    }
    CHECK(deliveries == 0, "DEFECT 1: valid-form noise after a packet delivers nothing");
  }

  // 5) DEFECT (4): a frame that lies about its payload length must be rejected.
  //
  // This is the memory-safety one. hdr.length is sender-declared and up to 255, while the receive
  // buffer is RS485_RECV_BUFFER (64) — so returning it unchecked hands the caller a length that runs
  // ~190 bytes past the end of a malloc'd buffer. The library's own checks for this exist but sit
  // inside `#if DEBUG_LEVEL >= DEBUG_TRACE`, and this test is compiled with no DEBUG_LEVEL at all,
  // i.e. exactly the release configuration that ships.
  {
    RS485Socket sock;
    fresh_socket(sock, MY_ADDR);
    const uint8_t shortPayload[] = { 0xFC, 0x00, 0x02, 0x0B };
    std::vector<uint8_t> msg =
        socket_msg(OTHER_ADDR, MY_ADDR, shortPayload, sizeof(shortPayload), 255 /* lie */);
    std::vector<uint8_t> framed = wire_frame(msg.data(), (uint8_t)msg.size());
    Serial.feed(framed.data(), framed.size());

    unsigned int retlen = 0;
    const byte *data = sock.getMsg(MY_ADDR, &retlen);
    CHECK(data == NULL, "DEFECT 4: frame declaring more payload than arrived is rejected");
    CHECK(retlen == 0, "DEFECT 4: retlen is cleared on rejection, not left at the declared 255");
  }

  // 6) DEFECT (4): a frame shorter than the socket header is rejected without being dereferenced.
  {
    RS485Socket sock;
    fresh_socket(sock, MY_ADDR);

    /*
     * Deliver a good frame addressed to us FIRST, then the runt. Without that priming step this is a
     * probabilistic test rather than a control: the unfixed getMsg() reads hdr.address out of a
     * freshly-malloc'd buffer at offsets 4-5, which is uninitialised rather than out of bounds, so it
     * only misbehaves when that garbage happens to equal MY_ADDR -- on a clean heap the test passes
     * with the fix disabled. Priming leaves our own address sitting at those offsets, so the unfixed
     * code matches on the stale address and hands back a bogus payload every time.
     */
    std::vector<uint8_t> primer = socket_msg(OTHER_ADDR, MY_ADDR, PAYLOAD, sizeof(PAYLOAD));
    std::vector<uint8_t> primerFramed = wire_frame(primer.data(), (uint8_t)primer.size());
    Serial.feed(primerFramed.data(), primerFramed.size());
    unsigned int retlen = 0;
    CHECK(sock.getMsg(MY_ADDR, &retlen) != NULL, "primer frame delivered");

    const uint8_t runt[] = { 1, 2, 3 };   // shorter than sizeof(rs485_socket_hdr_t) == 7
    std::vector<uint8_t> framed = wire_frame(runt, (uint8_t)sizeof(runt));
    Serial.feed(framed.data(), framed.size());

    retlen = 0;
    CHECK(sock.getMsg(MY_ADDR, &retlen) == NULL, "DEFECT 4: sub-header-length frame is rejected");
    CHECK(retlen == 0, "DEFECT 4: retlen cleared for a runt frame");
  }

  // 7) DEFECT (3): a truncated frame must not hold the receiver open forever.
  //
  // Upstream documents this as the caller's job and provides isPacketStarted()/getPacketStartTime()
  // for it; RS485Utils used neither, so a frame that stops mid-flight left haveSTX_ set until the
  // next STX arrived — unbounded, and every byte in between fed into the partial packet.
  {
    RS485Socket sock;
    fresh_socket(sock, MY_ADDR);
    std::vector<uint8_t> msg = socket_msg(OTHER_ADDR, MY_ADDR, PAYLOAD, sizeof(PAYLOAD));
    std::vector<uint8_t> framed = wire_frame(msg.data(), (uint8_t)msg.size());
    framed.resize(framed.size() / 2);            // cut it off mid-frame
    Serial.feed(framed.data(), framed.size());

    unsigned int retlen = 0;
    CHECK(sock.getMsg(MY_ADDR, &retlen) == NULL, "truncated frame is not delivered");
    CHECK(sock.packetInProgress(), "a partial packet is in progress before the timeout");

    test_time_advance(RS485_PACKET_TIMEOUT_MS + 1);
    retlen = 0;
    CHECK(sock.getMsg(MY_ADDR, &retlen) == NULL, "still nothing delivered after the timeout");
    CHECK(!sock.packetInProgress(), "DEFECT 3: the stalled partial packet is abandoned on timeout");
  }

  // 8) DEFECT (3a): the timeout must not fire on a packet that was already delivered.
  //
  // Ordering inside getMsg() is load-bearing. After the deferred-reset fix, a delivered packet leaves
  // haveSTX_ set and startTime_ at that packet's STX time until the NEXT call — so a timeout check
  // placed before the deferred reset would read any long gap between calls as a truncated frame. Such
  // gaps are routine in the bridge: loop() bails entirely while strip.isUpdating(), and serviceTx()
  // blocks ~48 ms per frame in serial->flush().
  {
    RS485Socket sock;
    fresh_socket(sock, MY_ADDR);
    std::vector<uint8_t> msg = socket_msg(OTHER_ADDR, MY_ADDR, PAYLOAD, sizeof(PAYLOAD));
    std::vector<uint8_t> framed = wire_frame(msg.data(), (uint8_t)msg.size());
    Serial.feed(framed.data(), framed.size());

    unsigned int retlen = 0;
    CHECK(sock.getMsg(MY_ADDR, &retlen) != NULL, "packet delivered");
    unsigned long timeoutsBefore = sock.getTimeoutCount();

    test_time_advance(1000);                     // a very long gap between polls
    retlen = 0;
    sock.getMsg(MY_ADDR, &retlen);
    CHECK(sock.getTimeoutCount() == timeoutsBefore,
          "DEFECT 3a: no timeout counted for an already-delivered packet");
  }

  // 9) A good frame arriving right after a timeout reset still parses — the timeout must not eat
  //    healthy traffic that follows it.
  {
    RS485Socket sock;
    fresh_socket(sock, MY_ADDR);
    std::vector<uint8_t> msg = socket_msg(OTHER_ADDR, MY_ADDR, PAYLOAD, sizeof(PAYLOAD));

    std::vector<uint8_t> truncated = wire_frame(msg.data(), (uint8_t)msg.size());
    truncated.resize(truncated.size() / 2);
    Serial.feed(truncated.data(), truncated.size());
    unsigned int retlen = 0;
    sock.getMsg(MY_ADDR, &retlen);
    test_time_advance(RS485_PACKET_TIMEOUT_MS + 1);
    retlen = 0;
    sock.getMsg(MY_ADDR, &retlen);               // trips the timeout, resets the channel

    std::vector<uint8_t> good = wire_frame(msg.data(), (uint8_t)msg.size());
    Serial.feed(good.data(), good.size());
    retlen = 0;
    const byte *data = sock.getMsg(MY_ADDR, &retlen);
    CHECK(data != NULL, "a good frame after a timeout reset is still delivered");
    CHECK(retlen == sizeof(PAYLOAD), "and with the right length");
  }

  // 10) DEFECT (2): a failed receive-buffer allocation must be visible, not silent.
  //
  // begin() does not check malloc. It fails *safe* — update() returns false when data_ == NULL — but
  // the result is a receiver dead for the whole boot while the bridge's /json/info reports a healthy
  // bridge, which is the worst possible thing to debug in the field. The shim's malloc hook forces
  // the exact failure that cannot be induced on a bench.
  {
    RS485Socket sock;
    Serial.clear();
    test_time_set(1000);
    test_malloc_fail_next(true);
    sock.init(&Serial, 5, MY_ADDR, RS485_RECV_BUFFER, false);
    sock.setup();                                // the malloc inside begin() fails here
    test_malloc_fail_next(false);

    CHECK(!sock.initialized(),
          "DEFECT 2: a socket whose receive buffer failed to allocate reports itself uninitialised");

    // And it must still fail safe rather than crash.
    std::vector<uint8_t> msg = socket_msg(OTHER_ADDR, MY_ADDR, PAYLOAD, sizeof(PAYLOAD));
    std::vector<uint8_t> framed = wire_frame(msg.data(), (uint8_t)msg.size());
    Serial.feed(framed.data(), framed.size());
    unsigned int retlen = 0;
    CHECK(sock.getMsg(MY_ADDR, &retlen) == NULL, "DEFECT 2: and delivers nothing rather than crashing");
  }

  // 11) A socket that was never init()ed must not be dereferenced by getMsg().
  //
  // `new RS485(...)` in init_general() is unchecked too, and getMsg() called channel->update() with
  // no NULL guard — so unlike begin()'s malloc, that path was a null-deref crash rather than silent
  // deafness.
  {
    RS485Socket sock;                            // default-constructed: channel == NULL
    unsigned int retlen = 0;
    CHECK(sock.getMsg(MY_ADDR, &retlen) == NULL, "getMsg on an uninitialised socket returns NULL");
    CHECK(!sock.initialized(), "an uninitialised socket says so");
  }

  // 12) Two complete frames arriving in one burst — the most common real sequence, and the one that
  //     most directly exercises the deferred reset: update() returns at the first frame's CRC with the
  //     second frame still sitting in the queue.
  {
    RS485Socket sock;
    fresh_socket(sock, MY_ADDR);
    const uint8_t SECOND[] = { 0xFC, 0x11, 0x22, 0x33 };
    std::vector<uint8_t> m1 = socket_msg(OTHER_ADDR, MY_ADDR, PAYLOAD, sizeof(PAYLOAD));
    std::vector<uint8_t> m2 = socket_msg(OTHER_ADDR, MY_ADDR, SECOND, sizeof(SECOND));
    std::vector<uint8_t> f1 = wire_frame(m1.data(), (uint8_t)m1.size());
    std::vector<uint8_t> f2 = wire_frame(m2.data(), (uint8_t)m2.size());
    Serial.feed(f1.data(), f1.size());
    Serial.feed(f2.data(), f2.size());

    unsigned int retlen = 0;
    const byte *d1 = sock.getMsg(MY_ADDR, &retlen);
    CHECK(d1 != NULL && retlen == sizeof(PAYLOAD), "back-to-back: first frame delivered");
    CHECK(d1 != NULL && memcmp(d1, PAYLOAD, sizeof(PAYLOAD)) == 0, "back-to-back: first payload right");
    CHECK(sock.getLength() == (byte)m1.size(), "back-to-back: getLength() tracks the first frame");

    retlen = 0;
    const byte *d2 = sock.getMsg(MY_ADDR, &retlen);
    CHECK(d2 != NULL && retlen == sizeof(SECOND), "back-to-back: second frame delivered");
    CHECK(d2 != NULL && memcmp(d2, SECOND, sizeof(SECOND)) == 0, "back-to-back: second payload right");
    CHECK(sock.getLength() == (byte)m2.size(), "back-to-back: getLength() tracks the second frame");
  }

  // 13) The rest of a frame arriving after a long poll gap must still be delivered.
  //
  // This is the case that makes the timeout ordering observable, and it is a REGRESSION guard, not a
  // feature test: startTime_ is stamped when update() parses the STX, so the elapsed time the timeout
  // sees is poll latency, not wire time. Check the timeout before update() and this frame is thrown
  // away even though its remaining bytes are already in the FIFO — traffic that worked fine before the
  // timeout existed. Running the check after update() drains the queue first, so a completable frame
  // completes and only a genuinely stalled one is abandoned.
  {
    RS485Socket sock;
    fresh_socket(sock, MY_ADDR);
    std::vector<uint8_t> msg = socket_msg(OTHER_ADDR, MY_ADDR, PAYLOAD, sizeof(PAYLOAD));
    std::vector<uint8_t> framed = wire_frame(msg.data(), (uint8_t)msg.size());

    size_t split = framed.size() / 2;
    Serial.feed(framed.data(), split);                       // first half arrives
    unsigned int retlen = 0;
    CHECK(sock.getMsg(MY_ADDR, &retlen) == NULL, "partial frame not yet delivered");

    test_time_advance(RS485_PACKET_TIMEOUT_MS + 50);         // caller was busy (strip update, flash write)
    Serial.feed(framed.data() + split, framed.size() - split);   // the rest is already in the FIFO

    retlen = 0;
    const byte *data = sock.getMsg(MY_ADDR, &retlen);
    CHECK(data != NULL, "frame completed across a long poll gap is still delivered");
    CHECK(retlen == sizeof(PAYLOAD), "and with the right length");
    CHECK(sock.getTimeoutCount() == 0, "and no timeout was counted for it");
  }

  // 14) The two getMsg() overloads share one msgPending contract; interleaving them must be safe.
  {
    RS485Socket sock;
    fresh_socket(sock, MY_ADDR);
    std::vector<uint8_t> msg = socket_msg(OTHER_ADDR, MY_ADDR, PAYLOAD, sizeof(PAYLOAD));
    std::vector<uint8_t> framed = wire_frame(msg.data(), (uint8_t)msg.size());
    Serial.feed(framed.data(), framed.size());

    unsigned int retlen = 0;
    CHECK(sock.getMsg(&retlen) != NULL, "the sourceAddress overload delivers");  // filters on MY_ADDR

    std::vector<uint8_t> staleTrigger;
    wire_push_byte(staleTrigger, wire_crc8(msg.data(), (uint8_t)msg.size()));
    Serial.feed(staleTrigger.data(), staleTrigger.size());

    retlen = 0;
    CHECK(sock.getMsg(RS485_ADDR_ANY, &retlen) == NULL,
          "a packet consumed via one overload is not re-delivered by the other");
  }

  // 15) The timeout scales with the line rate.
  //
  // Without this, a full-size frame at a slow baud is longer on the wire than the fixed default and is
  // abandoned mid-flight every time — the receiver stays deaf to large frames while small ones get
  // through, which is a miserable thing to diagnose. baud is user-settable in the bridge's config, so
  // this is reachable from the settings page, not just in theory.
  {
    RS485Socket sock;
    fresh_socket(sock, MY_ADDR);
    std::vector<uint8_t> msg = socket_msg(OTHER_ADDR, MY_ADDR, PAYLOAD, sizeof(PAYLOAD));
    std::vector<uint8_t> framed = wire_frame(msg.data(), (uint8_t)msg.size());

    sock.setPacketTimeoutForBaud(4800);                      // ~275 ms for a maximal frame
    Serial.feed(framed.data(), framed.size() / 2);
    unsigned int retlen = 0;
    sock.getMsg(MY_ADDR, &retlen);

    test_time_advance(RS485_PACKET_TIMEOUT_MS + 50);         // past the DEFAULT, not past the derived one
    retlen = 0;
    sock.getMsg(MY_ADDR, &retlen);
    CHECK(sock.packetInProgress(),
          "a slow-baud frame is not abandoned at the default timeout");
    CHECK(sock.getTimeoutCount() == 0, "and nothing was counted against it");

    test_time_advance(2000);                                 // now past the derived timeout too
    retlen = 0;
    sock.getMsg(MY_ADDR, &retlen);
    CHECK(!sock.packetInProgress(), "but it is abandoned once the derived timeout expires");
    CHECK(sock.getTimeoutCount() == 1, "and counted");
  }

  printf("\n%d checks, %d failures\n", checks, failures);
  printf(failures ? "SOME TESTS FAILED\n" : "ALL TESTS PASSED\n");
  return failures ? 1 : 0;
}
