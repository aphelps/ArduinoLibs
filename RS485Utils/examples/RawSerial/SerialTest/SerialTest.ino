#include <Arduino.h>
#include <SoftwareSerial.h>
#include <stdio.h>

#ifndef SERIAL_TYPE
#define SERIAL_TYPE 1
#endif

#if SERIAL_TYPE == 0
  SoftwareSerial rs485 (4, 7);  // receive pin, transmit pin
  #define ENABLE_PIN 2
#elif SERIAL_TYPE == 1
  #define rs485 Serial1
  #define ENABLE_PIN 23
  #define HARDWARE_SERIAL
#endif

#define LED_PIN 13

void setup()
{
  Serial.begin(9600);

  rs485.begin(28800);

  pinMode (LED_PIN, OUTPUT);  // driver output enable

  pinMode (ENABLE_PIN, OUTPUT);  // driver output enable
  digitalWrite(ENABLE_PIN, LOW);

  Serial.println("Initiating raw serial master");
}

int i = 0;
boolean b = false;

#define BUF_LEN 32
char buffer[BUF_LEN];

void send_data(char *data, unsigned int datalen) {
  digitalWrite(ENABLE_PIN, HIGH);
  rs485.write(data, datalen);
#ifdef HARDWARE_SERIAL
  // XXX: A delay may be required here with hardware serial, or checking registers based on the serial device: http://www.gammon.com.au/forum/?id=11428
  //delayMicroseconds(350 * datalen);
  rs485.flush(); // Waits for the send buffer to empty
#endif
  digitalWrite(ENABLE_PIN, LOW);

  Serial.print("Sent: ");
  Serial.print(datalen);
  Serial.print(": ");
  Serial.println(data);
}

int read_data(char *buff, int bufflen) {
  char *ptr = buff;
  int count = 0;
  while (rs485.available() && count < (bufflen - 1)) {
    *ptr = rs485.read();
    ptr++;
    count++;
  }
  buff[bufflen - 1] = '\0';

  if (count) {
    Serial.print("Read: ");
    Serial.print(count);
    Serial.print(": ");
    Serial.println(buff);
  }

  return count;
}

void loop() {
  read_data(buffer, BUF_LEN);

  i++;
  snprintf(buffer, BUF_LEN, "abcdef%d\0", i);
  send_data(buffer, strlen(buffer));

  b = !b;
  digitalWrite(LED_PIN, b);

  delay(1000);
}
