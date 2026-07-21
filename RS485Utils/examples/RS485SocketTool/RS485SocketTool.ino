/*
 * Example of a bidirectional RS485Socket client
 *
 * Author: Adam Phelps
 * License: MIT
 * Copyright: 2018
 */

#ifdef DEBUG_LEVEL_RS485SOCKETTOOL
  #define DEBUG_LEVEL DEBUG_LEVEL_RS485SOCKETTOOL
#endif
#ifndef DEBUG_LEVEL
  #define DEBUG_LEVEL DEBUG_HIGH
#endif
#include "Debug.h"
#include "Socket.h"
#include "RS485Utils.h"

#define STATUS_LED LED_BUILTIN

/* Configuration section */
#ifndef PIN_RS485_RECV /* All pins should be defined by compiler flags */
  #define PIN_RS485_RECV    7
  #define PIN_RS485_XMIT    4
  #define PIN_RS485_ENABLED 2
#endif
#ifndef ADDRESS
  #define ADDRESS 128
#endif

#define DATA_SIZE 64
#define SEND_BUFFER_SIZE RS485_BUFFER_TOTAL(DATA_SIZE)
byte databuffer[SEND_BUFFER_SIZE];
byte *send_buffer;

RS485Socket rs485(PIN_RS485_RECV, PIN_RS485_XMIT, PIN_RS485_ENABLED, (DEBUG_LEVEL != 0));

void setup() {
  Serial.begin(115200);
  pinMode(STATUS_LED, OUTPUT);

  rs485.setup();
  send_buffer = rs485.initBuffer(databuffer, DATA_SIZE);

  DEBUG1_PRINTLN("*** RS485SocketTool initialized ***")
}

#define SEND_PERIOD 1000
unsigned long last_send_ms = 0;
byte count = 0;

void loop() {
  unsigned long now = millis();

  if (now - SEND_PERIOD >= last_send_ms) {
    send_buffer[0] = 'T';
    send_buffer[1] = count++;
    DEBUG1_VALUELN("* Sending ", count);

    rs485.sendMsgTo(SOCKET_ADDR_ANY, send_buffer, 2);

    last_send_ms = now;
  }

  unsigned int retlen;
  const byte *data = rs485.getMsg(&retlen);
  if (data != NULL) {
    DEBUG1_VALUE("* Received data ", retlen);
    DEBUG1_PRINT(": ");
    print_hex_buffer((char *)data, retlen);
    DEBUG_PRINT_END();
  }

  delay(10);
}