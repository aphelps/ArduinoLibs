/*******************************************************************************
 * Author: Adam Phelps
 * License: MIT
 * Copyright: 2014
 *
 * Socket wrapper of RFM69 code:
 *   https://github.com/LowPowerLab/RFM69
 *******************************************************************************/

#ifndef RM69SOCKET_H
#define RM69SOCKET_H

#include "Arduino.h"
#include "Socket.h"
#include <RFM69.h>

/*
 * On-wire socket header: 6 bytes, on every target.
 *
 * Packed for the same reason as rs485_socket_hdr_t (see RS485Utils.h for the full rationale), but this
 * one is the worst of the three: unpacked, `source` sits at offset 1 on AVR and offset 2 on a
 * 2-byte-aligned ABI, because the ABI inserts an *interior* pad after `ID`. So the addresses
 * themselves move, not just the payload — a 32-bit RFM69 node would misread who a frame is from and
 * who it is for. Packing pins the AVR layout, which is the one two shipping firmwares already speak.
 *
 * Unlike XBeeSocket, this is NOT prophylactic in the AVR direction: rfm69_socket_hdr_t is compiled
 * into [env:moteino] and [env:nano_rfm69] in HMTL/platformio/HMTL_Module/platformio.ini, both
 * -DUSE_RFM69. That makes the static_asserts below the safety argument for this change — if packing
 * moved anything on AVR, two modules in the field would change on the wire.
 */
typedef struct __attribute__((__packed__)) {
  byte ID;
  socket_addr_t source; // TODO: Are these necessary?
  socket_addr_t address;
  byte flags;
} rfm69_socket_hdr_t;
/* Calculate the total buffer size with a useable buffer of size x */
#define RFM69_BUFFER_TOTAL(x) (uint8_t)(x + sizeof (rfm69_socket_hdr_t))
#define RFM69_DATA_LENGTH(x) (uint8_t)(x - sizeof (rfm69_socket_hdr_t))

/*
 * Convert an address such that the RFM69.h broadcast address is a Socket.h
 * broadcast address.
 */
#define RFM69_BROADCAST_CONVERT(x) ((x == RF69_BROADCAST_ADDR) ? SOCKET_ADDR_ANY : x)

typedef struct __attribute__((__packed__)) {
  rfm69_socket_hdr_t hdr;
  byte              data[];
} rfm69_socket_msg_t;

#if defined(__cplusplus) && __cplusplus >= 201103L
#include <stddef.h>
static_assert(sizeof(rfm69_socket_hdr_t) == 6,
              "rfm69_socket_hdr_t must be 6 bytes on the wire (AVR layout) on every target");
static_assert(offsetof(rfm69_socket_hdr_t, ID)      == 0, "rfm69 socket hdr: ID at 0");
static_assert(offsetof(rfm69_socket_hdr_t, source)  == 1, "rfm69 socket hdr: source at 1, no interior pad");
static_assert(offsetof(rfm69_socket_hdr_t, address) == 3, "rfm69 socket hdr: address at 3");
static_assert(offsetof(rfm69_socket_hdr_t, flags)   == 5, "rfm69 socket hdr: flags at 5");
static_assert(offsetof(rfm69_socket_msg_t, data) == 6, "rfm69 socket payload starts at offset 6");
#endif


class RFM69Socket : public Socket {

public:
  /* RFM69 specific functions */
  RFM69Socket();
  RFM69Socket(socket_addr_t _address, uint8_t _network_ID,
             uint8_t _irq_pin, boolean _high_power, uint8_t _freq);
  void init(socket_addr_t _address, uint8_t _network_ID,
            uint8_t _irq_pin, boolean _high_power, uint8_t _freq);

  void setEncryptionKey(const char* key);

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
  RFM69 *radio;
  byte currentMsgID;
};

#endif
