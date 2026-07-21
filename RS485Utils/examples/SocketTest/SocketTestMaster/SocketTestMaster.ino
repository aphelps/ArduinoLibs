/*
 * Example of a minimal RS485Socket master
 */
#ifndef DEBUG_LEVEL
  #define DEBUG_LEVEL DEBUG_HIGH
#endif
#include "Debug.h"

#include <RS485_non_blocking.h>
#ifdef __AVR__
#include <SoftwareSerial.h>
#endif

#include "Socket.h"
#include "RS485Utils.h"

// The following pins should be adjusted based on your setup, or passed via compiler flags
#ifndef PIN_RS485_XMIT
#define PIN_RS485_XMIT        7
#endif
#ifndef PIN_RS485_RECV
#define PIN_RS485_RECV        4
#endif
#ifndef PIN_RS485_ENABLED
#define PIN_RS485_ENABLED     2
#endif

#define DEBUG_PIN 13

RS485Socket rs485(PIN_RS485_RECV, PIN_RS485_XMIT, PIN_RS485_ENABLED, (DEBUG_LEVEL != 0));

#define DATA_SIZE 64
#define SEND_BUFFER_SIZE RS485_BUFFER_TOTAL(DATA_SIZE)
byte databuffer[SEND_BUFFER_SIZE];
byte *send_buffer;

void setup()
{
  Serial.begin(9600);
  pinMode(DEBUG_PIN, OUTPUT);

  rs485.setup();
  send_buffer = rs485.initBuffer(databuffer, DATA_SIZE);
}

#define MY_ADDR   0
#define DEST_ADDR 1

byte count = 0;
boolean debugOn = false;
void loop()
{
  send_buffer[0] = 'T';
  send_buffer[1] = count++;

  DEBUG1_VALUELN("Sending ", count);

  rs485.sendMsgTo(DEST_ADDR, send_buffer, 2);

  debugOn = !debugOn;
  if (debugOn)
    digitalWrite(DEBUG_PIN, HIGH);
  else
    digitalWrite(DEBUG_PIN, LOW);
  delay(1000);
}
