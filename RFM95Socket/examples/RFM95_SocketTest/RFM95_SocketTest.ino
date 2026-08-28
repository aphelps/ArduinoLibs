/*
 * RFM95_SocketTest — two-node send/receive check for RFM95Socket.
 *
 * Flash this to TWO SparkFun ESP32 LoRa 1-CH Gateways with different -DNODE_ADDRESS values and they
 * talk to each other. It is written to be the bench test rather than a demo, so it reports the
 * things that actually distinguish the failure modes:
 *
 *   * Whether the radio answered at all. That is one bit and it covers a wrong CS, a wrong SPI bus
 *     and a dead module equally -- so the sketch prints the pin map it used alongside it, because
 *     when this fails the pin map is the thing you need to check and re-deriving it at the bench is
 *     how a session gets lost.
 *
 *   * Counts of packets ACCEPTED and packets REJECTED BY THE ADDRESS FILTER, separately. Only the
 *     second number tells you addressing works. A node that ignored hdr.address entirely would show
 *     a healthy accepted-count and look perfect -- which is the trap, since LoRa has no addressing
 *     of its own and every node hears every packet.
 *
 *   * RSSI, printed but explicitly NOT treated as health. A good RSSI on a packet that failed its
 *     address filter says the radio works and says nothing about whether addressing does.
 *
 * Build:
 *   cd ArduinoLibs/platformio/RFM95Socket/RFM95_SocketTest
 *   pio run -e esp32_lora_gw_a -t upload      # node A, address 0x0001
 *   pio run -e esp32_lora_gw_b -t upload      # node B, address 0x0002
 *   pio device monitor -b 115200
 */

#include <SPI.h>
#include <LoRa.h>
#include <Socket.h>
#include <RFM95Socket.h>

#ifndef NODE_ADDRESS
#define NODE_ADDRESS 0x0001
#endif
#ifndef PEER_ADDRESS
#define PEER_ADDRESS 0x0002
#endif

#define SEND_INTERVAL_MS 3000

RFM95Socket radio;
byte  buffer[64];
byte *send_data;

unsigned long last_send = 0;
uint32_t sent = 0, accepted = 0, rejected = 0;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("RFM95_SocketTest");

  radio.init(NODE_ADDRESS);
  radio.setup();

  Serial.print("  address:   0x"); Serial.println(NODE_ADDRESS, HEX);
  Serial.print("  peer:      0x"); Serial.println(PEER_ADDRESS, HEX);
  Serial.println("  freq:      915 MHz");
  Serial.println("  SPI:       SCK 14 / MISO 12 / MOSI 13");
  Serial.println("  radio:     CS 16 / DIO0 26 / RESET -1 (not wired on this board)");

  if (radio.initialized()) {
    Serial.println("  radio:     OK");
  } else {
    /*
     * Everything that can be wrong presents here identically. Listing the candidates beats a bare
     * "init failed", because the next step differs completely between them.
     */
    Serial.println("  radio:     NOT RESPONDING");
    Serial.println("             version register did not read 0x12. Candidates, all indistinguishable");
    Serial.println("             from here: wrong CS pin, SPI bound to the wrong bus, dead module,");
    Serial.println("             or the antenna-less transmit that killed the PA.");
  }

  send_data = radio.initBuffer(buffer, sizeof(buffer));
}

void loop() {
  unsigned int len = 0;

  /*
   * Read with our own address so the filter is exercised. Note what is NOT done here: reading with
   * SOCKET_ADDR_ANY would accept everything and make the rejected-count permanently zero, which
   * would look like a clean run.
   */
  const byte *msg = radio.getMsg(NODE_ADDRESS, &len);
  if (msg != NULL) {
    accepted++;
    Serial.print("RX  from 0x");
    Serial.print(radio.sourceFromData((void *)msg), HEX);
    Serial.print("  len ");
    Serial.print(len);
    Serial.print("  rssi ");
    Serial.print(radio.packetRssi());
    Serial.print("  payload ");
    for (unsigned int i = 0; i < len; i++) {
      Serial.print(msg[i], HEX);
      Serial.print(' ');
    }
    Serial.println();
  } else if (radio.getLength() > 0) {
    /*
     * getLength() is non-zero only when a frame was received, read whole, and then turned down by
     * the address filter. A malformed frame clears it, so this counts filtering specifically rather
     * than every non-delivery -- which is what makes the number mean something.
     */
    rejected++;
    Serial.print("RX  rejected (not addressed to us), total ");
    Serial.println(rejected);
  }

  if (millis() - last_send > SEND_INTERVAL_MS) {
    last_send = millis();

    send_data[0] = (byte)(sent & 0xFF);
    send_data[1] = (byte)((sent >> 8) & 0xFF);
    send_data[2] = 0x5A;
    send_data[3] = 0xA5;
    radio.sendMsgTo(PEER_ADDRESS, send_data, 4);
    sent++;

    Serial.print("TX  to 0x");
    Serial.print(PEER_ADDRESS, HEX);
    Serial.print("  seq ");
    Serial.print(sent);
    Serial.print("   [sent ");
    Serial.print(sent);
    Serial.print(" / accepted ");
    Serial.print(accepted);
    Serial.print(" / rejected ");
    Serial.print(rejected);
    Serial.println("]");
  }
}
