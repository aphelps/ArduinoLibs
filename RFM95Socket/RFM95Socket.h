/*******************************************************************************
 * Author: Adam Phelps
 * License: MIT
 * Copyright: 2026
 *
 * Socket wrapper of the RFM95 (LoRa) radio, using:
 *   https://github.com/sandeepmistry/arduino-LoRa
 *
 * Modelled on RFM69Socket. The important difference is that arduino-LoRa has NO
 * node addressing of its own -- it is a raw packet pipe. RFM69Socket can lean on
 * the driver for a destination byte and a broadcast address; here the entire
 * address space lives in rfm95_socket_hdr_t and every received packet is filtered
 * in software. See getMsg().
 *
 * This transport is UNENCRYPTED. The RFM69 has AES in hardware and RFM69Socket
 * uses it; the RFM95 does not have it, and no software cipher is applied here.
 * That is a deliberate decision, not an oversight -- see README.md.
 *******************************************************************************/

#ifndef RFM95SOCKET_H
#define RFM95SOCKET_H

#include "Arduino.h"
#include "Socket.h"

/*
 * On-wire socket header: 6 bytes, on every target.
 *
 * Same field set and same packing rationale as rfm69_socket_hdr_t (see
 * RFM69Socket.h, and RS485Utils.h for the full argument). Unpacked, `source`
 * would sit at offset 1 on AVR and offset 2 on a 2-byte-aligned ABI, because the
 * ABI inserts an interior pad after `ID` -- so the addresses themselves would
 * move, not just the payload.
 *
 * Unlike rfm69_socket_hdr_t, this one is PROPHYLACTIC in the AVR direction: there
 * is no AVR LoRa node today, and the only build that compiles this library is an
 * ESP32 one. The asserts exist so that if one ever appears, the wire format it
 * meets is the AVR layout rather than whatever the ESP32 compiler chose. The host
 * suite compiles this header a second time under -fpack-struct=1 (the AVR-layout
 * proxy) to keep that claim checked rather than merely intended.
 *
 * Deliberately GENERIC, not LoRa-flavoured: LoRa<->RS485/HMTL bridging is planned.
 * Nothing radio-specific (RSSI, SNR, spreading factor) belongs on the wire, because
 * a bridge would then have to strip it. Those live on the status API below instead.
 */
typedef struct __attribute__((__packed__)) {
  byte ID;
  socket_addr_t source;
  socket_addr_t address;
  byte flags;
} rfm95_socket_hdr_t;

typedef struct __attribute__((__packed__)) {
  rfm95_socket_hdr_t hdr;
  byte               data[];
} rfm95_socket_msg_t;

/* Calculate the total buffer size with a useable buffer of size x */
#define RFM95_BUFFER_TOTAL(x) (uint8_t)((x) + sizeof (rfm95_socket_hdr_t))
#define RFM95_DATA_LENGTH(x)  (uint8_t)((x) - sizeof (rfm95_socket_hdr_t))

/*
 * The SX127x FIFO is 256 bytes and arduino-LoRa's explicit-header mode caps a
 * packet at 255. That is the ceiling on a whole frame, header included.
 *
 * Socket::sendMsgTo takes `const byte length` and Socket::getLength returns `byte`,
 * so the base class already pins a payload to one byte -- 255 fits with nothing to
 * widen. Anything larger would be a change to Socket.h and all four transports.
 */
#define RFM95_MAX_PACKET      255
#define RFM95_MAX_DATA_LEN    (RFM95_MAX_PACKET - sizeof (rfm95_socket_hdr_t))

/*
 * Pin map for the SparkFun ESP32 LoRa 1-CH Gateway (WRL-15006).
 *
 * Verified against the schematic NETLIST (sparkfunX/ESP32_LoRa_1CH_Gateway,
 * Hardware/esp32_lora_gateway.sch -- EAGLE 9 schematics are XML), not against a
 * reading of the schematic PDF and not against SparkFun's example sketches, which
 * disagree with each other about RESET.
 *
 * RESET IS NOT WIRED TO THE ESP32. Net RFM_RST reaches U2.!RESET and R5.1 and
 * stops there; R5.2 is on the 3.3V net, so the line is a 10k pull-up and nothing
 * else. The radio cannot be hardware-reset in software on this board. Passing -1
 * is therefore correct and not a placeholder: LoRa.cpp guards its reset sequence
 * on `_reset != -1`. SparkFun's own sketches pass 5 (Firmware/) or 27 (Production/)
 * -- two values in one repo, neither connected to anything, both broken out to
 * headers where driving them would be someone else's pin.
 */
#define RFM95_SPARKFUN_1CH_SCK    14
#define RFM95_SPARKFUN_1CH_MISO   12
#define RFM95_SPARKFUN_1CH_MOSI   13
#define RFM95_SPARKFUN_1CH_CS     16
#define RFM95_SPARKFUN_1CH_RESET  -1
#define RFM95_SPARKFUN_1CH_DIO0   26

/* 915 MHz, the US ISM band this board ships for. */
#define RFM95_FREQ_US 915000000L

#if defined(__cplusplus) && __cplusplus >= 201103L
#include <stddef.h>
static_assert(sizeof(rfm95_socket_hdr_t) == 6,
              "rfm95_socket_hdr_t must be 6 bytes on the wire (AVR layout) on every target");
static_assert(offsetof(rfm95_socket_hdr_t, ID)      == 0, "rfm95 socket hdr: ID at 0");
static_assert(offsetof(rfm95_socket_hdr_t, source)  == 1, "rfm95 socket hdr: source at 1, no interior pad");
static_assert(offsetof(rfm95_socket_hdr_t, address) == 3, "rfm95 socket hdr: address at 3");
static_assert(offsetof(rfm95_socket_hdr_t, flags)   == 5, "rfm95 socket hdr: flags at 5");
static_assert(offsetof(rfm95_socket_msg_t, data)    == 6, "rfm95 socket payload starts at offset 6");
#endif


class RFM95Socket : public Socket {

public:
  RFM95Socket();

  /*
   * Defaults are the SparkFun 1-CH Gateway's pins, which is the only board this
   * has been built for. Pass explicit pins for anything else.
   *
   * Nothing here touches hardware -- SPI and the radio are brought up in setup().
   * That is a deliberate divergence from RFM69Socket, whose init() constructs the
   * radio and calls initialize() before setup() ever runs. Starting SPI from a
   * constructor runs before the Arduino core is ready when the socket is a global.
   */
  RFM95Socket(socket_addr_t _address, long _frequency = RFM95_FREQ_US);
  void init(socket_addr_t _address, long _frequency = RFM95_FREQ_US);

  /*
   * Override the radio's pins. Must be called BEFORE setup(); afterwards it has
   * no effect, because setup() is what hands them to SPI and the driver.
   *
   * setSPIPins() is not optional plumbing on ESP32 -- see the .cpp for why leaving
   * SPI at its default is the likeliest way to get a silently dead radio.
   */
  void setRadioPins(int _cs, int _reset, int _dio0);
  void setSPIPins(int _sck, int _miso, int _mosi);

  /* Radio status. Not on the wire, on purpose -- see the header comment. */
  int   packetRssi();
  float packetSnr();

  /*
   * Implement functions from Socket.h
   */
  void setup();
  boolean initialized();
  byte * initBuffer(byte * data, uint16_t data_size);

  void sendMsgTo(uint16_t address, const byte * data, const byte length);

  const byte *getMsg(unsigned int *retlen);
  const byte *getMsg(uint16_t address, unsigned int *retlen);

  byte getLength();
  void *headerFromData(const void *data);
  socket_addr_t sourceFromData(void *data);
  socket_addr_t destFromData(void *data);

private:
  long frequency;
  int  cs_pin, reset_pin, dio0_pin;
  int  sck_pin, miso_pin, mosi_pin;

  boolean radio_ready;
  byte    currentMsgID;

  /*
   * Receive staging. RFM69Socket can hand back a pointer into radio->DATA because
   * the RFM69 driver owns a frame buffer; arduino-LoRa exposes only a Stream, so
   * the frame has to be read out somewhere before it can be addressed as a struct.
   */
  byte recv_buffer[RFM95_MAX_PACKET];
  byte recv_length;
};

#endif
