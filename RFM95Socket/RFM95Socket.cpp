/*******************************************************************************
 * Author: Adam Phelps
 * License: MIT
 * Copyright: 2026
 *
 * Socket wrapper of the RFM95 (LoRa) radio
 */

#ifdef DEBUG_LEVEL_RFM95SOCKET
#define DEBUG_LEVEL DEBUG_LEVEL_RFM95SOCKET
#endif
#ifndef DEBUG_LEVEL
  #define DEBUG_LEVEL DEBUG_TRACE
#endif
#include "Debug.h"

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>

#include "Socket.h"
#include "RFM95Socket.h"

RFM95Socket::RFM95Socket() {
  frequency   = RFM95_FREQ_US;
  cs_pin      = RFM95_SPARKFUN_1CH_CS;
  reset_pin   = RFM95_SPARKFUN_1CH_RESET;
  dio0_pin    = RFM95_SPARKFUN_1CH_DIO0;
  sck_pin     = RFM95_SPARKFUN_1CH_SCK;
  miso_pin    = RFM95_SPARKFUN_1CH_MISO;
  mosi_pin    = RFM95_SPARKFUN_1CH_MOSI;
  radio_ready = false;
  currentMsgID = 0;
  recv_length = 0;
  sourceAddress = SOCKET_ADDR_INVALID;
}

RFM95Socket::RFM95Socket(socket_addr_t _address, long _frequency) : RFM95Socket() {
  init(_address, _frequency);
}

void RFM95Socket::init(socket_addr_t _address, long _frequency) {
  sourceAddress = _address;
  frequency     = _frequency;
}

void RFM95Socket::setRadioPins(int _cs, int _reset, int _dio0) {
  cs_pin    = _cs;
  reset_pin = _reset;
  dio0_pin  = _dio0;
}

void RFM95Socket::setSPIPins(int _sck, int _miso, int _mosi) {
  sck_pin  = _sck;
  miso_pin = _miso;
  mosi_pin = _mosi;
}

/*
 * Bring up SPI and the radio.
 *
 * The explicit SPI.begin() is the whole reason this is in setup() rather than
 * init(), and it is not defensive tidiness:
 *
 *   LoRa.begin() calls _spi->begin() on whatever SPIClass it was given, which
 *   defaults to the global `SPI`. On ESP32 with `board = esp32dev` that is VSPI --
 *   SCK 18 / MISO 19 / MOSI 23 / SS 5 -- and NONE of those reach the radio, whose
 *   bus is 14/12/13. So the default configuration does two bad things at once: it
 *   reads a version register from nothing (initialized() false, with no clue why),
 *   and it drives GPIO 23, which [env:esp32_lora_gw] has assigned as PIXELS_CLOCK.
 *
 *   SparkFun's own examples never hit this because their board variant
 *   (sparkx_esp32_lora/pins_arduino.h) redefines SS/MOSI/MISO/SCK to the radio's
 *   pins, so the "default" SPI is already correct there. We do not build with that
 *   variant, so we have to say it.
 *
 * KNOWN LIMIT, and it matters for the planned WLED integration. The ESP32 core's
 * SPIClass::begin() starts with `if (_spi) return;` -- if the global SPI object has
 * ALREADY been brought up by anything else, the pin arguments here are silently
 * discarded and the bus keeps whatever pins it was given first. WLED's bus_manager
 * also brings up SPI, so ordering there is not a detail.
 *
 * The host suite does NOT catch this: shim/SPI.h records the pins on every call,
 * which is more forgiving than the hardware, so the pin-map assertions stay green
 * in exactly the case that would fail on a board. Flagged in post-PR self-review;
 * fixing it properly means owning an SPIClass instance and handing it to
 * LoRa.setSPI() rather than sharing the global, which is a change to make when
 * there is a second SPI consumer to test against.
 */
void RFM95Socket::setup() {
  if (radio_ready) return;

  SPI.begin(sck_pin, miso_pin, mosi_pin, cs_pin);

  LoRa.setPins(cs_pin, reset_pin, dio0_pin);

  /*
   * begin() returns 0 when the version register does not read back 0x12, which is
   * a genuine "the radio did not answer" and covers a wrong CS, a wrong SPI bus or
   * a dead module alike. It cannot tell those apart -- neither can the bench test.
   */
  radio_ready = (LoRa.begin(frequency) == 1);

  if (radio_ready) {
    DEBUG3_VALUE("RFM95Socket: addr:", sourceAddress);
    DEBUG3_VALUE(" freq:", frequency);
    DEBUG3_VALUE(" cs:", cs_pin);
    DEBUG3_VALUE(" dio0:", dio0_pin);
    DEBUG3_VALUELN(" rst:", reset_pin);
  } else {
    DEBUG_ERR("RFM95Socket: radio did not respond");
  }
}

boolean RFM95Socket::initialized() {
  return radio_ready;
}

/*
 * Deliberate divergence from RFM69Socket: an oversized buffer is CLAMPED and
 * reported, not turned into DEBUG_ERR_STATE().
 *
 * debug_err_state() is `while (true)` flashing pin 13 -- on an AVR with an LED on
 * 13 that is a diagnostic, but this library only ever builds for ESP32, where it
 * is an unkillable loop in a library call with no LED on the other end. A caller
 * that over-sizes its buffer should get a working radio and a complaint, not a
 * wedged board.
 */
byte *RFM95Socket::initBuffer(byte *data, uint16_t data_size) {
  if (data_size > RFM95_MAX_PACKET) {
    DEBUG_ERR("RFM95Socket: sndbuf too big, clamping");
    data_size = RFM95_MAX_PACKET;
  }
  /*
   * A buffer smaller than the header has no room for a payload at all. Without this the subtraction
   * below underflows uint16_t -- data_size 0 gives send_data_size 65530 -- and the caller is handed
   * a send_buffer pointing past the end of its own array along with a size saying 64 KB is
   * available. Report zero instead, which is the truth.
   */
  if (data_size < sizeof (rfm95_socket_hdr_t)) {
    DEBUG_ERR("RFM95Socket: sndbuf smaller than the header");
    send_data_size = 0;
    send_buffer = data;
    return send_buffer;
  }
  send_data_size = data_size - sizeof (rfm95_socket_hdr_t);
  send_buffer = data + sizeof (rfm95_socket_hdr_t);
  return send_buffer;
}

#if DEBUG_LEVEL >= DEBUG_TRACE
static void printSocketMsg(const rfm95_socket_msg_t *msg, byte length) {
  DEBUG4_HEXVAL(" id:",  msg->hdr.ID);
  DEBUG4_HEXVAL(" source:", msg->hdr.source);
  DEBUG4_HEXVAL(" addr:", msg->hdr.address);
  DEBUG4_HEXVAL(" flags:", msg->hdr.flags);
  DEBUG4_PRINT(" data:");
  print_hex_buffer((char *)msg->data, length);
}
#endif

void RFM95Socket::sendMsgTo(uint16_t address, const byte * data, const byte datalength) {
  if (!radio_ready) return;

  /*
   * Bounds check, and it is not defensive padding -- without it this function puts MALFORMED FRAMES
   * ON THE AIR, silently.
   *
   * RFM95_BUFFER_TOTAL() casts to uint8_t, and `datalength` is a `byte` because Socket pins it to
   * one, so 250..255 are all expressible and all wrap:
   *
   *   sendMsgTo(..., 250) -> RFM95_BUFFER_TOTAL == 0   -> an EMPTY frame transmitted
   *   sendMsgTo(..., 255) -> RFM95_BUFFER_TOTAL == 5   -> a 5-byte RUNT frame transmitted
   *
   * A runt is exactly what getMsg()'s unconditional length check exists to reject -- so without this
   * the library's transmit path manufactures the frames its own receive path is defending against,
   * and a peer running this same code drops them with "runt frame" while the sender reports success.
   *
   * RFM69Socket needs no equivalent because RF69_MAX_DATA_LEN is 61 and the sum cannot reach 256.
   * Here RFM95_MAX_PACKET is exactly 255, which is what makes the overflow reachable.
   *
   * Dropped rather than clamped, deliberately: a clamped send would put a well-formed frame on the
   * air with the tail of the caller's message missing, which the receiver cannot detect. A drop is
   * loud and loses the same message.
   */
  if (datalength > RFM95_MAX_DATA_LEN) {
    DEBUG_ERR("RFM95Socket: payload over 249B, send dropped");
    return;
  }

  rfm95_socket_msg_t *msg = (rfm95_socket_msg_t *)headerFromData(data);

  msg->hdr.ID = currentMsgID++;
  msg->hdr.source = sourceAddress;
  msg->hdr.address = address;
  msg->hdr.flags = 0;

  /*
   * There is no driver-level destination to set. Unlike RFM69Socket, which sends
   * to (address & 0xFF) or RF69_BROADCAST_ADDR, every LoRa packet reaches every
   * listening radio on the frequency, and hdr.address is the only thing that says
   * who it is for. SOCKET_ADDR_ANY therefore needs no conversion on the way out --
   * it travels as itself.
   */
#if DEBUG_LEVEL == DEBUG_TRACE
  DEBUG5_VALUE("-> XMIT:", datalength);
  DEBUG5_VALUE(" addr:", address);
  printSocketMsg(msg, datalength);
  DEBUG_PRINT_END();
#endif

  /* Returns 0 if a previous async send is still in flight. */
  if (LoRa.beginPacket() == 0) {
    DEBUG_ERR("RFM95Socket: busy, send dropped");
    return;
  }
  LoRa.write((const uint8_t *)msg, RFM95_BUFFER_TOTAL(datalength));
  LoRa.endPacket();
}

const byte *RFM95Socket::getMsg(unsigned int *retlen) {
  return getMsg(sourceAddress, retlen);
}

const byte *RFM95Socket::getMsg(socket_addr_t address, unsigned int *retlen) {
  *retlen = 0;
  recv_length = 0;

  if (!radio_ready) return NULL;

  int packetSize = LoRa.parsePacket();
  if (packetSize <= 0) return NULL;

  /*
   * Length checks come BEFORE the struct cast, and they are unconditional -- not
   * inside a DEBUG_LEVEL guard. RFM69Socket's equivalent check lives inside
   * `#if DEBUG_LEVEL == DEBUG_TRACE`, so in the release build that ships it does
   * not exist and a runt frame is cast and read past its end. Same defect class
   * the RS485 receive suite was written for.
   */
  if (packetSize > RFM95_MAX_PACKET) {
    DEBUG_ERR("RFM95Socket: oversized frame");
    while (LoRa.available()) LoRa.read();
    return NULL;
  }
  if (packetSize < (int)sizeof (rfm95_socket_hdr_t)) {
    DEBUG_ERR("RFM95Socket: runt frame");
    while (LoRa.available()) LoRa.read();
    return NULL;
  }

  int i = 0;
  while (LoRa.available() && i < packetSize) {
    recv_buffer[i++] = (byte)LoRa.read();
  }
  if (i != packetSize) {
    /* Stream ran dry early: the frame we have is not the frame we were told about. */
    DEBUG_ERR("RFM95Socket: short read");
    return NULL;
  }
  recv_length = (byte)packetSize;

  const rfm95_socket_msg_t *msg = (const rfm95_socket_msg_t *)recv_buffer;

#if DEBUG_LEVEL == DEBUG_TRACE
  DEBUG5_VALUE("<- RCV:", recv_length);
  printSocketMsg(msg, RFM95_DATA_LENGTH(recv_length));
  DEBUG_PRINT_END();
#endif

  /*
   * Software address filtering, and it is the ONLY filtering there is. Every node
   * on the frequency receives this packet; without this check every node would
   * process it. SOCKET_ADDRESS_MATCH handles SOCKET_ADDR_ANY on either side, so no
   * broadcast conversion is needed -- the RFM69 needs RFM69_BROADCAST_CONVERT only
   * because its driver has a separate broadcast address of its own.
   */
  if (SOCKET_ADDRESS_MATCH(address, msg->hdr.address)) {
    *retlen = RFM95_DATA_LENGTH(recv_length);
    return &(msg->data[0]);
  }

  DEBUG5_VALUE("Not for me: ", address);
  DEBUG5_VALUELN(" to:", msg->hdr.address);
  return NULL;
}

/* Total length of the last received frame, header included -- matches RFM69Socket. */
byte RFM95Socket::getLength() {
  return recv_length;
}

void *RFM95Socket::headerFromData(const void *data) {
  return ((rfm95_socket_hdr_t *)((long)data - sizeof (rfm95_socket_hdr_t)));
}

socket_addr_t RFM95Socket::sourceFromData(void *data) {
  return ((rfm95_socket_hdr_t *)headerFromData(data))->source;
}

socket_addr_t RFM95Socket::destFromData(void *data) {
  return ((rfm95_socket_hdr_t *)headerFromData(data))->address;
}

/*
 * Link quality for the last received packet.
 *
 * Deliberately an API rather than a header field. Also deliberately NOT used as a
 * health check anywhere: a healthy RSSI on a packet that failed its address filter
 * says the radio works and says nothing about whether addressing does. That is the
 * reassuring-but-uninformative diagnostic the RS485 bring-up already got caught by.
 */
int RFM95Socket::packetRssi() {
  return LoRa.packetRssi();
}

float RFM95Socket::packetSnr() {
  return LoRa.packetSnr();
}
