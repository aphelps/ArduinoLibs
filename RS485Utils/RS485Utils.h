/*******************************************************************************
 * Author: Adam Phelps
 * License: Create Commons Attribution-Share-Alike
 * Copyright: 2014
 *
 * This class provides a socket-like API for communicating via RS485.
 *
 * This relies on Nick Gammon's RS485 non-blocking protocol library to provide
 * the underlying transport-layer protocol. 
 * (See http://www.gammon.com.au/forum/?id=11428)
 *
 * TODO: With the addition of socket-level
 * CRC checks a lighter-weight protocol could be used to achieve higher line
 * speeds.
 ******************************************************************************/

#ifndef RS485UTILS_H
#define RS485UTILS_H

#include "Arduino.h"
#include "Socket.h"

#include <RS485_non_blocking.h>

#ifdef __AVR__
#include <SoftwareSerial.h>
#endif

// TODO: Should be specified by the sketch that creates the socket rather than
// being statically defined
#define RS485_RECV_BUFFER 64 // 140 // XXX: This is a lot of buffer space

/*
 * How long a partially-received packet may sit unfinished before the receiver gives up on it.
 *
 * Nick Gammon's library deliberately leaves this to the caller (it exposes isPacketStarted() and
 * getPacketStartTime() for the purpose and says so in a comment), and nothing here picked it up — so a
 * frame truncated mid-flight left haveSTX_ set until the next STX arrived, however long that took,
 * with every byte in between fed into the partial packet.
 *
 * 250 ms is derived, not guessed. RS485_RECV_BUFFER is the whole frame budget, and Gammon's encoding
 * sends each byte as two nibble-complemented bytes plus STX, ETX and a 2-byte CRC, so a maximal frame
 * is 64*2 + 4 = 132 bytes on the wire; at DEFAULT_BAUD (28000, 8N1 -> 2800 B/s) that is ~47 ms. 250 ms
 * is therefore ~5x headroom, and it replaces a bound that was previously unbounded. Raise it if you run
 * a slower bus or a larger RS485_RECV_BUFFER.
 */
#ifndef RS485_PACKET_TIMEOUT_MS
#define RS485_PACKET_TIMEOUT_MS 250
#endif

/*
 * By default this code uses a software serial device.  If the RS485 chip is
 * attached to the hardware serial pins then those can be used instead by
 * specifying the port to use with the RS485_HARDWARE_SERIAL compile flag.
 *
 * SoftwareSerial is AVR-only, so on any other platform RS485_HARDWARE_SERIAL is
 * mandatory.  When neither backend is available the library compiles to nothing
 * (RS485UTILS_SUPPORTED == 0) rather than erroring out: that keeps this
 * directory harmless when a larger project (e.g. a WLED build matrix) has
 * ArduinoLibs on lib_extra_dirs and the library dependency finder pulls it in
 * without the RS485 flags.  Guard any use of RS485Socket with
 * `#if RS485UTILS_SUPPORTED`.
 */
//#define RS485_HARDWARE_SERIAL Serial1
#if defined(RS485_HARDWARE_SERIAL)
#define RS485UTILS_SUPPORTED 1
#define SERIAL_TYPE HardwareSerial
#elif defined(__AVR__)
#define RS485UTILS_SUPPORTED 1
#define SERIAL_TYPE SoftwareSerial
#else
#define RS485UTILS_SUPPORTED 0
#endif

#if RS485UTILS_SUPPORTED


// "Broadcast" address
#define RS485_ADDR_ANY SOCKET_ADDR_ANY

// "Invalid address"
#define RS485_ADDR_INVALID SOCKET_ADDR_INVALID

/*
 * The on-wire socket header: 7 bytes, on every target.
 *
 * PACKED IS LOAD-BEARING, not a micro-optimisation. sizeof() here is what positions the payload at
 * both ends — sendMsgTo() writes the header at (data - sizeof(hdr)) and puts sizeof(hdr) + datalength
 * on the wire, and getMsg() reads the payload from msg->data. Unpacked, that size is 7 on AVR
 * (alignment 1) but 8 on any 2-byte-aligned ABI, which rounds 7 up to a trailing pad byte. An ESP32
 * would then emit [7 header][1 pad][payload] while an ATMega328 module reads the payload from offset
 * 7 — every frame between the two misparsed by one byte, in both directions. Packing makes the
 * 32-bit layout match the deployed AVR fleet's, which is the one that defines the wire; it is
 * layout-neutral on AVR (nothing there was padded to begin with), and the static_asserts below say so
 * rather than leaving it to be trusted.
 *
 * Field offsets were already identical across ABIs; only the size differed. Packing also makes the
 * three places that cast a raw buffer straight to this struct (sendMsgTo, getMsg, headerFromData)
 * unconditionally safe: with alignment 1 the compiler emits byte-wise accesses, where before the
 * 16-bit source/address loads were correctly aligned only by accident of where the buffer landed.
 */
typedef struct __attribute__((__packed__)) {
  byte     ID;
  byte     length;
	socket_addr_t source;
	socket_addr_t address;
  byte     flags;
} rs485_socket_hdr_t;

/* Calculate the total buffer size with a useable buffer of size x */
#define RS485_BUFFER_TOTAL(x) (x + sizeof (rs485_socket_hdr_t))

typedef struct __attribute__((__packed__)) {
  rs485_socket_hdr_t hdr;
  byte               data[];
} rs485_socket_msg_t;

/*
 * Pin the layout so a future ABI, a reordered field or a dropped attribute fails the build instead of
 * silently breaking interoperability with modules already in the field. The size assert is the one
 * that matters for the wire; the offsets are asserted because they are what every cast through
 * rs485_socket_msg_t depends on.
 *
 * offsetof() on the flexible array member `data` is a GCC extension, and is what actually pins the
 * payload origin. Kept alongside the sizeof so the two can never drift apart.
 */
#if defined(__cplusplus) && __cplusplus >= 201103L
#include <stddef.h>
static_assert(sizeof(rs485_socket_hdr_t) == 7,
              "rs485_socket_hdr_t must be 7 bytes on the wire (AVR layout) on every target");
static_assert(offsetof(rs485_socket_hdr_t, ID)      == 0, "rs485 socket hdr: ID at 0");
static_assert(offsetof(rs485_socket_hdr_t, length)  == 1, "rs485 socket hdr: length at 1");
static_assert(offsetof(rs485_socket_hdr_t, source)  == 2, "rs485 socket hdr: source at 2");
static_assert(offsetof(rs485_socket_hdr_t, address) == 4, "rs485 socket hdr: address at 4");
static_assert(offsetof(rs485_socket_hdr_t, flags)   == 6, "rs485 socket hdr: flags at 6");
static_assert(sizeof(rs485_socket_msg_t) == 7, "rs485_socket_msg_t is header-sized (data[] is a FAM)");
static_assert(offsetof(rs485_socket_msg_t, data) == 7, "rs485 socket payload starts at offset 7");
#endif

// Get the socket header from the data portion of a message
#define RS485_HDR_FROM_DATA(x) ((rs485_socket_hdr_t *)((long)x - sizeof (rs485_socket_hdr_t))) // DEPRECATED

// Get the source address from the data portion of a message
#define RS485_SOURCE_FROM_DATA(x) (RS485_HDR_FROM_DATA(x)->source) // DEPRECATED

// Get the destination address from the data portion of a message
#define RS485_ADDRESS_FROM_DATA(x) (RS485_HDR_FROM_DATA(x)->address) // DEPRECATED

void printSocketMsg(const rs485_socket_msg_t *msg);
void printBuffer(const byte *buff, int length);

class RS485Socket : public Socket
{
  public:
  RS485Socket();
	RS485Socket(SERIAL_TYPE *_serial, byte _enablePin, socket_addr_t _address);
  RS485Socket(byte _recvPin, byte _xmitPin, byte _enablePin, socket_addr_t _address);
  RS485Socket(byte _recvPin, byte _xmitPin, byte _enablePin, socket_addr_t _address,
	      boolean debug);

	void init(SERIAL_TYPE *_serial, byte _enablePin, socket_addr_t _address,
						byte _recvsize, boolean debug);
  void init(byte _recvPin, byte _xmitPin, byte _enablePin, socket_addr_t _address,
	    byte _recvsize, boolean _debug);

  void setup();
  byte * initBuffer(byte * data, uint16_t data_size);
  byte * initBuffer(byte * data);

  void sendMsgTo(socket_addr_t address, const byte * data, const byte length);

  /*
   * Receive one message, or NULL if none is available or it was not for `address`.
   *
   * POINTER LIFETIME — read this before changing anything in here. The returned pointer aliases the
   * channel's receive buffer; it stays valid, and getLength() keeps reporting this frame's length,
   * until the NEXT call to either getMsg() overload. That contract is what lets a caller do
   *
   *     data = getMsg(&len);  ...  socketLen = getLength();   // still this frame
   *
   * which the WLED rs485_bridge relies on to bounds-check the frame after the fact. The consumed
   * packet is cleared at the TOP of the following call (see the deferred reset in the .cpp), not on
   * the way out of this one, precisely so that ordering is safe. Copy the payload out before calling
   * again if you need to keep it.
   */
  const byte *getMsg(socket_addr_t address, unsigned int *retlen);
  const byte *getMsg(unsigned int *retlen);
  byte getLength();
  void *headerFromData(const void *data);
  socket_addr_t sourceFromData(void *data);
  socket_addr_t destFromData(void *data);

  /*
   * True once the socket is usable: serial set, channel allocated, AND the channel's receive buffer
   * successfully allocated. That last term matters — RS485::begin() mallocs without checking, and a
   * NULL buffer makes update() return false forever, i.e. a receiver silently dead for the whole boot
   * while everything else looks healthy. Callers should treat false here as a hard setup failure and
   * say so, rather than running on in a state where no frame can ever arrive.
   */
  boolean initialized();

  /*
   * True while a partial packet is being assembled (an STX has been seen, no complete frame yet).
   * Exposed mainly so tests and diagnostics can observe the timeout doing its job.
   */
  boolean packetInProgress();

  /* How many partial packets have been abandoned by the receive timeout since boot. */
  uint16_t getTimeoutCount();

  /*
   * Scale the packet timeout to the line rate. The default is sized for DEFAULT_BAUD; a slower bus
   * needs longer, because a maximal frame legitimately takes longer to arrive. Call after init() if
   * the port runs at anything other than DEFAULT_BAUD -- otherwise a full-size frame at, say, 4800
   * baud (275 ms on the wire) would be abandoned mid-flight every single time and the receiver would
   * be permanently deaf to large frames while small ones got through.
   */
  void setPacketTimeoutForBaud(unsigned long baud);

  /*
   * How many complete packets have been rejected by the socket-layer length checks since boot.
   *
   * Exists so a rejection stays observable to the caller. getMsg() returns NULL both for "nothing
   * arrived" and "something arrived and was malformed", and consumers reasonably treat NULL as an
   * empty bus and stop draining. Without this, moving the length checks out of the debug guard would
   * have traded a memory-safety hole for a silent one: bad frames would stop being counted anywhere.
   */
  uint16_t getRejectCount();

  /*
   * Framing errors seen by THIS receiver, as counted by the underlying protocol library:
   * a byte that failed the nibble-complement form check, a CRC mismatch, or a receive-buffer
   * overflow. RS485_non_blocking lumps all three into one counter, so this cannot tell them apart --
   * the name says "framing" rather than "overflow" for that reason.
   *
   * Two things it deliberately does NOT tell you, both worth knowing before reading a non-zero value:
   *   * On a real bus the likeliest cause is line noise or missing termination, not overflow. Treat a
   *     slowly climbing value as a wiring question, not a software one.
   *   * It cannot see a frame THIS node sent being dropped by a peer whose buffer was too small --
   *     that overflow happens in the peer's channel, and a half-duplex sender does not hear itself.
   *     Use the transmit-side length checks for that.
   *
   * Counts from RS485::begin() -- i.e. from setup(), not from construction: errorCount_ is zeroed in
   * begin() and is NOT in the RS485 constructor's initialiser list, so between init() and setup() it
   * is indeterminate. Do not read it before setup().
   *
   * uint32_t, not uint16_t, because the underlying counter is an unsigned long and bumps once per bad
   * byte-pair, per bad CRC and per overflow. Narrowing would wrap at 65536 in a months-long
   * deployment, and a wrap presents as the counter FALLING -- which is exactly the reading the note
   * above tells an operator means healthy.
   */
  uint32_t getFramingErrorCount();

  byte recvLimit;
	socket_addr_t sourceAddress;

	// TODO: This could likely be a higher rate, especially on devices with
	//       hardware serial.  However all connected devices would need to use
	//       the same speed
	static const int DEFAULT_BAUD = 28000;

 private:
  byte enablePin;
  byte currentMsgID;

  RS485 *channel;

  /*
   * True when update() has handed us a completed packet that the caller has now been given (or
   * rejected). The channel is reset at the top of the NEXT getMsg() rather than immediately, which is
   * what keeps getData()/getLength() valid for as long as the caller holds the pointer — see the
   * lifetime note on getMsg().
   *
   * Set on EVERY path taken after update() returns true, not just the one that returns a payload. The
   * address-mismatch and length-check paths consume a packet too, and for consumers that filter on
   * their own address (HMTL_Module, HMTL_Command_CLI) mismatch is the common case on a shared bus, so
   * arming this only on delivery would leave the stale-packet window permanently open for exactly the
   * callers that need it closed.
   */
  boolean msgPending;

  /*
   * Diagnostic counters. uint16_t rather than unsigned long deliberately: these are per-socket
   * members and RS485Socket ships on ATmega328 parts with 2 KB of SRAM, so the pair costs 4 bytes
   * instead of 8. Wrapping is harmless -- they exist to answer "is this happening?", and the one
   * programmatic consumer (the WLED bridge) compares before/after around a single call, which
   * survives a wrap.
   */
  uint16_t timeoutCount;
  uint16_t rejectCount;

  /* Effective packet timeout; RS485_PACKET_TIMEOUT_MS unless setPacketTimeoutForBaud() raises it. */
  unsigned long packetTimeoutMs;

  static size_t serialWrite(const byte what);
  static size_t serialDebugWrite(const byte what);
  static int serialRead();
  static int serialDebugRead();
  static int serialAvailable();

	void init_general(SERIAL_TYPE *_serial,  byte _enablePin,
										socket_addr_t _address, byte _recvsize,
										boolean _debug);
};

#endif // RS485UTILS_SUPPORTED

#endif // RS485UTILS_H
