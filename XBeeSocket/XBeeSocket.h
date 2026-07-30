/*******************************************************************************
 * Author: Adam Phelps
 * License: MIT
 * Copyright: 2015
 *
 * XBee implementation of the socket class
 */

#ifndef XBEESOCKET_H
#define XBEESOCKET_H

#include "Arduino.h"
#include "XBee.h"
#include "Socket.h"

/*
 * On-wire socket header: 5 bytes, on every target.
 *
 * Packed for the same reason as rs485_socket_hdr_t (see RS485Utils.h for the full rationale):
 * sizeof() positions the payload at both ends, and unpacked this struct is 5 bytes on AVR but 6 on a
 * 2-byte-aligned ABI, which would misplace the payload by one byte between an AVR node and a 32-bit
 * one. Field offsets already agreed; only the trailing pad differed. Layout-neutral on AVR.
 *
 * Prophylactic here rather than a live bug fix: nothing builds XBeeSocket today — HMTLTypes.cpp gates
 * it on USE_XBEE and HMTL's module platformio.ini hardcodes -DDISABLE_XBEE — so this exists to stop
 * the same defect reaching whoever writes the next 32-bit transport.
 */
typedef struct __attribute__((__packed__)) {
  socket_addr_t source;
  socket_addr_t address;
  byte flags;
} xbee_socket_hdr_t;

/* Calculate the total buffer size with a useable buffer of size x */
#define XBEE_BUFFER_TOTAL(x) (uint8_t)(x + sizeof (xbee_socket_hdr_t))
#define XBEE_DATA_LENGTH(x) (uint8_t)(x - sizeof (xbee_socket_hdr_t))

typedef struct __attribute__((__packed__)) {
  xbee_socket_hdr_t hdr;
  byte              data[];
} xbee_socket_msg_t;

#if defined(__cplusplus) && __cplusplus >= 201103L
#include <stddef.h>
static_assert(sizeof(xbee_socket_hdr_t) == 5,
              "xbee_socket_hdr_t must be 5 bytes on the wire (AVR layout) on every target");
static_assert(offsetof(xbee_socket_hdr_t, source)  == 0, "xbee socket hdr: source at 0");
static_assert(offsetof(xbee_socket_hdr_t, address) == 2, "xbee socket hdr: address at 2");
static_assert(offsetof(xbee_socket_hdr_t, flags)   == 4, "xbee socket hdr: flags at 4");
static_assert(offsetof(xbee_socket_msg_t, data) == 5, "xbee socket payload starts at offset 5");
#endif

class XBeeSocket : public Socket {
 public:
  XBeeSocket();
  XBeeSocket(XBee *xbee, socket_addr_t _address);
  void init(XBee *xbee, socket_addr_t _address);
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

  byte recvLimit;
  socket_addr_t sourceAddress;

 private:

  XBee *xbee;
  ZBRxResponse rx;

};

#endif
