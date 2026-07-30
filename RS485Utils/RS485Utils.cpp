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

#include <RS485_non_blocking.h>

#ifdef __AVR__
#include <SoftwareSerial.h>
#endif

#ifdef DEBUG_LEVEL_RS485UTILS
  #define DEBUG_LEVEL DEBUG_LEVEL_RS485UTILS
#endif

#ifndef DEBUG_LEVEL
//  #define DEBUG_LEVEL DEBUG_HIGH
#endif
#include "Debug.h"

#include "RS485Utils.h"

// No usable serial backend on this platform (see RS485Utils.h): compile to nothing rather than
// failing, so a project that merely has ArduinoLibs on lib_extra_dirs still builds.
#if RS485UTILS_SUPPORTED

// XXX: Need to figure out how to use non-static function pointers so this
//      can be a class field.
//
// This must stay `static` (internal linkage): only this translation unit's
// static callbacks touch it, and a bare global named `serial` is a link-time
// collision hazard once RS485Utils.cpp is compiled into a larger image (e.g.
// the WLED firmware).
static SERIAL_TYPE *serial;

RS485Socket::RS485Socket() {
  enablePin = 0;
  serial = NULL;
  channel = NULL;
  msgPending = false;
  timeoutCount = 0;
  rejectCount = 0;
  packetTimeoutMs = RS485_PACKET_TIMEOUT_MS;
}

RS485Socket::RS485Socket(SERIAL_TYPE *_serial, byte _enablePin,
                         socket_addr_t _address) {
  serial = NULL;
  init(_serial, _enablePin, _address, RS485_RECV_BUFFER, false);
}

RS485Socket::RS485Socket(byte _recvPin, byte _xmitPin, byte _enablePin,
                         socket_addr_t _address)
{
  serial = NULL;
  init(_recvPin, _xmitPin, _enablePin, _address, RS485_RECV_BUFFER, false);
}

RS485Socket::RS485Socket(byte _recvPin, byte _xmitPin, byte _enablePin,
                         socket_addr_t _address, boolean _debug)
{
  serial = NULL;
  init(_recvPin, _xmitPin, _enablePin, _address, RS485_RECV_BUFFER, _debug);
}

void RS485Socket::init(SERIAL_TYPE *_serial, byte _enablePin,
                       socket_addr_t _address, byte _recvsize, boolean debug) {
  init_general(_serial, _enablePin, _address, _recvsize, debug);
}

void RS485Socket::init(byte _recvPin, byte _xmitPin, byte _enablePin,
                       socket_addr_t _address, byte _recvsize, boolean _debug) {
  if (serial != NULL) {
    DEBUG_ERR("RS485Socket::init already initialized");
    DEBUG_ERR_STATE(DEBUG_ERR_REINIT);
    // XXX - Could re-init the config?
  } else {
#ifdef RS485_HARDWARE_SERIAL

#if defined(ESP32)
    serial = new HardwareSerial(RS485_HARDWARE_SERIAL);
    serial->begin(DEFAULT_BAUD, SERIAL_8N1, _recvPin, _xmitPin);
#else
    serial = &RS485_HARDWARE_SERIAL;
#endif

#else
    serial = new SoftwareSerial(_recvPin, _xmitPin);
#endif
    init_general(serial, _enablePin, _address, _recvsize, _debug);
  }
}

void RS485Socket::init_general(SERIAL_TYPE *_serial, byte _enablePin,
                               socket_addr_t _address, byte _recvsize,
                               boolean _debug) {
  enablePin = _enablePin;
  sourceAddress = _address;
  recvLimit = _recvsize;
  serial = _serial;

  pinMode(enablePin, OUTPUT);
  channel = new RS485(serialRead, serialAvailable, serialWrite,
                      recvLimit);

  currentMsgID = 0;
  msgPending = false;
  timeoutCount = 0;
  rejectCount = 0;
  packetTimeoutMs = RS485_PACKET_TIMEOUT_MS;
}

/*
 * A socket is only usable if all three of these hold. The third is the one that used to be missing:
 * RS485::begin() allocates the receive buffer with an unchecked malloc, and a NULL buffer makes
 * update() return false forever — a receiver silently dead for the whole boot while the rest of the
 * system looks healthy. getData() is public and returns that buffer, so testing it costs nothing and
 * needs no change to the vendored library.
 *
 * Note this can only be meaningful after setup(), which is what calls begin().
 */
boolean RS485Socket::initialized() {
  return (serial != NULL) && (channel != NULL) && (channel->getData() != NULL);
}

boolean RS485Socket::packetInProgress() {
  if (channel == NULL) return false;
  return channel->isPacketStarted();
}

uint16_t RS485Socket::getTimeoutCount() {
  return timeoutCount;
}

uint16_t RS485Socket::getRejectCount() {
  return rejectCount;
}

uint32_t RS485Socket::getFramingErrorCount() {
  // Null-guarded like packetInProgress()/getLength(): a socket whose channel allocation failed must
  // report zero rather than dereference. Note the value is only MEANINGFUL once setup() has run --
  // initialized() is the stronger test, but returning 0 for an un-set-up socket is the same answer.
  if (channel == NULL) return 0;
  // No narrowing: getErrorCount() is an unsigned long, and truncating it would make a wrap look like
  // the counter going down.
  return (uint32_t)channel->getErrorCount();
}

void RS485Socket::setup() 
{
  if (serial == NULL) {
    DEBUG_ERR("RS485Socket::setup called before initialized");
    DEBUG_ERR_STATE(DEBUG_ERR_UNINIT);
  }

  DEBUG5_VALUELN("RS485 setup: ", DEFAULT_BAUD);
#if !defined(ESP32)
  serial->begin(DEFAULT_BAUD);
#endif

  /*
   * Guard the allocation from init_general(). On AVR a failed `new` returns NULL, and the AVR fleet is
   * precisely the case that matters here — without this, the crash landed inside setup() one line
   * before any caller could ask initialized() whether the socket came up.
   */
  if (channel == NULL) {
    DEBUG_ERR("RS485Socket::setup no channel");
    return;
  }

  channel->begin();

  pinMode(enablePin, OUTPUT);

  digitalWrite(enablePin, LOW);
}


#if DEBUG_LEVEL == DEBUG_TRACE
size_t RS485Socket::serialWrite(const byte value) 
{
  DEBUG4_PRINT("_");
  if (value <= 0xF) {
    DEBUG4_HEX(0);
  }
  DEBUG4_HEX(value);

  return serial->write(value);
}

int RS485Socket::serialRead() 
{
  int value = serial->read();
  DEBUG4_PRINT("-");
  if (value <= 0xF) {
    DEBUG4_HEX(0);
  }
  DEBUG4_HEX(value);
  return value;
}
#else
size_t RS485Socket::serialWrite(const byte value)
{
  return serial->write(value);
}

int RS485Socket::serialRead()
{
  return serial->read();
}

#endif

int RS485Socket::serialAvailable() 
{
  return serial->available();
}

/*
 * Initialize a data buffer.  The actual buffer should be at least a header
 * larger than the specified data_size.
 */
byte * RS485Socket::initBuffer(byte * data, uint16_t data_size) 
{
  DEBUG4_VALUELN("initBuffer: size:", data_size);
  send_data_size = data_size - sizeof (rs485_socket_hdr_t);
  send_buffer = data + sizeof (rs485_socket_hdr_t);
  return send_buffer;
}

byte * RS485Socket::initBuffer(byte * data) {
  DEBUG1_PRINTLN("initbuffer() deprecated");
  return initBuffer(data, 0);
}

void RS485Socket::sendMsgTo(socket_addr_t address,
                            const byte *data,
                            const byte datalength) 
{
  rs485_socket_msg_t *msg =
    (rs485_socket_msg_t *)(data - sizeof (rs485_socket_hdr_t));

  unsigned int msg_len = sizeof (rs485_socket_hdr_t) + datalength;

  msg->hdr.ID = currentMsgID++;
  msg->hdr.length = datalength;
  msg->hdr.source = sourceAddress;
  msg->hdr.address = address;
  msg->hdr.flags = 0;

#if DEBUG_LEVEL == DEBUG_TRACE
  DEBUG_ENDLN();
  DEBUG5_VALUE("XMIT:", msg_len);
  DEBUG5_PRINT(" ");
  printSocketMsg(msg);
  DEBUG_ENDLN()
#endif

  digitalWrite(enablePin, HIGH);
  channel->sendMsg((byte *)msg, msg_len);
#ifdef RS485_HARDWARE_SERIAL
  // XXX: A delay is required here with hardware serial, or checking registers based on the serial device: http://www.gammon.com.au/forum/?id=11428 or possibly use a timer to disable enablePin
  serial->flush(); // Block s until the send buffer is empty
#endif
  digitalWrite(enablePin, LOW);
}

const byte *RS485Socket::getMsg(unsigned int *retlen) {
  return getMsg(sourceAddress, retlen);
}

/*
 * Receive one message. See the pointer-lifetime note on the declaration in RS485Utils.h.
 *
 * THE ORDER OF THE THREE STEPS BELOW IS LOAD-BEARING: deferred reset, then timeout, then update().
 *
 *  1. Deferred reset. A packet handed out (or rejected) on the previous call is cleared now rather
 *     than then, which is what keeps getData()/getLength() valid while the caller still holds the
 *     pointer. Clearing it at all is the fix for the real bug: the vendored update() sets available_
 *     and returns true WITHOUT resetting, so haveSTX_/haveETX_/inputPos_ stayed live past the packet.
 *     Any subsequent valid-form byte then completed against a still-set haveETX_ and was tested as a
 *     CRC over the PREVIOUS packet's buffer — and on a match, update() returned true again and the
 *     caller re-processed a stale payload as fresh. Harmless for a colour command; not harmless for
 *     SET_ADDRESS, which consumers persist.
 *
 *  2. Timeout, and only after the reset. If it ran first, a delivered packet — which leaves haveSTX_
 *     set and startTime_ at its own STX time until this next call — would make any long gap between
 *     polls look like a truncated frame. Those gaps are routine (the WLED bridge skips its whole loop
 *     while the LED strip is updating, and blocks ~48 ms per transmitted frame in serial->flush()).
 *     After step 1 the channel is already clear, so isPacketStarted() is false and this cannot
 *     misfire. Gate on isPacketStarted() first regardless: reset() zeroes startTime_, so the elapsed
 *     comparison is meaningless on its own.
 *
 *  3. update(), which reads whatever has arrived.
 */
const byte *RS485Socket::getMsg(socket_addr_t address, unsigned int *retlen)
{
  /*
   * Nothing to receive on a socket that was never init()ed. Guarding here matters because
   * init_general()'s `new RS485(...)` is unchecked: without this, a failed allocation was a null
   * dereference on the first receive rather than a quiet no-op.
   */
  if (channel == NULL) {
    *retlen = 0;
    return NULL;
  }

  // (1) Clear the packet consumed on the previous call.
  if (msgPending) {
    channel->reset();
    msgPending = false;
  }

  // (2) Read.
  if (channel->update()) {
    /*
     * A complete packet exists, so it is consumed whatever we decide about it below. Set the flag
     * BEFORE any of the early returns: the address-mismatch and length-rejection paths consume a
     * packet just as much as a successful delivery does, and for consumers that filter on their own
     * address (HMTL_Module, HMTL_Command_CLI) mismatch is the common case on a shared bus — arming
     * this only on the delivery path would leave the stale window permanently open for them.
     */
    msgPending = true;

#if DEBUG_LEVEL >= DEBUG_TRACE
    DEBUG_ENDLN()
    DEBUG5_VALUE("getMsg:", getLength());
#endif

    const rs485_socket_msg_t *msg = (rs485_socket_msg_t *)channel->getData();

    /*
     * Bounds checks, which used to live inside `#if DEBUG_LEVEL >= DEBUG_TRACE` and so existed only in
     * a trace-level debug build. In every release build getMsg() dereferenced msg->hdr.address out of
     * a buffer that might be shorter than the header, and returned *retlen = msg->hdr.length — a
     * sender-declared byte, up to 255 — pointing into a buffer of only recvLimit (default 64). A
     * caller that trusted it read well past the end of the allocation.
     *
     * Two checks are sufficient and a third would be redundant: the second implies
     * hdr.length <= getLength() - sizeof(hdr), and getLength() returns inputPos_, which update()
     * bounds by bufferSize_ == recvLimit. Only the tests and the returns came out of the debug guard;
     * the DEBUG_ERR reporting stays inside it.
     */
    if (getLength() < sizeof (rs485_socket_hdr_t)) {
      DEBUG_ERR("ERROR-length < header");
      rejectCount++;
      *retlen = 0;
      return NULL;
    }

    if (getLength() < (sizeof (rs485_socket_hdr_t) + msg->hdr.length)) {
      DEBUG_ERR("ERROR-length < header + data");
      rejectCount++;
      *retlen = 0;
      return NULL;
    }

#if DEBUG_LEVEL >= DEBUG_TRACE
    DEBUG4_PRINT(" RECV: ");
    printSocketMsg(msg);
    DEBUG_PRINT_END();
#endif

    if (SOCKET_ADDRESS_MATCH(address, msg->hdr.address)) {
      *retlen = msg->hdr.length;
      return &msg->data[0];
    }
  }

  /*
   * (3) Nothing completed. Only now consider abandoning a stalled partial packet.
   *
   * AFTER update(), never before. startTime_ is stamped when update() *parses* the STX, so the
   * elapsed time measured here includes however long the caller spent not polling — it is poll
   * latency, not wire time. Checking before update() would therefore discard a frame whose remaining
   * bytes were already sitting in the UART FIFO, simply because the caller was busy for a while
   * (WLED skips this usermod's whole loop while the LED strip is updating, blocks ~48 ms per
   * transmitted frame, and writes flash on SET_ADDRESS). Draining first means a frame that CAN
   * complete does complete, and only a genuinely stalled one is dropped — so this can no longer lose
   * traffic that worked before the timeout existed.
   */
  if (channel->isPacketStarted() &&
      (millis() - channel->getPacketStartTime() > packetTimeoutMs)) {
    channel->reset();
    timeoutCount++;
  }

  *retlen = 0;
  return NULL;
}

/*
 * The timeout has to track the line rate, because a maximal frame at a slow baud can legitimately take
 * longer than the default. RS485_RECV_BUFFER bytes go out as two nibble-complemented bytes each, plus
 * STX, ETX and a two-byte CRC, at 10 bits per byte; five times that is the budget, floored at the
 * compile-time default so a fast bus still tolerates poll jitter.
 *
 * At 28000 baud this yields the 250 ms floor; at 4800 it yields ~1.4 s, where a fixed 250 ms would have
 * killed every full-size frame mid-flight and left the receiver permanently deaf to large frames.
 */
void RS485Socket::setPacketTimeoutForBaud(unsigned long baud) {
  if (baud == 0) return;
  const unsigned long wireMs =
      ((unsigned long)RS485_RECV_BUFFER * 2UL + 4UL) * 10UL * 1000UL / baud;
  const unsigned long derived = wireMs * 5UL;
  packetTimeoutMs = (derived > RS485_PACKET_TIMEOUT_MS) ? derived : RS485_PACKET_TIMEOUT_MS;
}

byte RS485Socket::getLength()
{
  if (channel == NULL) return 0;
  return channel->getLength();
}

void *RS485Socket::headerFromData(const void *data) {
  return ((rs485_socket_hdr_t *)((long)data - sizeof (rs485_socket_hdr_t)));
}

socket_addr_t RS485Socket::sourceFromData(void *data) {
  return ((rs485_socket_hdr_t *)headerFromData(data))->source;
}

socket_addr_t RS485Socket::destFromData(void *data) {
  return ((rs485_socket_hdr_t *)headerFromData(data))->address;
}


#if DEBUG_LEVEL >= DEBUG_TRACE
void printSocketMsg(const rs485_socket_msg_t *msg) 
{
  DEBUG4_VALUE( "i:",  msg->hdr.ID);
  DEBUG4_VALUE( " l:", msg->hdr.length);
  DEBUG4_VALUE( " s:", msg->hdr.source);
  DEBUG4_VALUE( " a:", msg->hdr.address);
  DEBUG4_HEXVAL( " f:", msg->hdr.flags);
  DEBUG4_PRINT(" data:");
  print_hex_buffer((char *)msg->data, msg->hdr.length);
}
#endif

#endif // RS485UTILS_SUPPORTED
