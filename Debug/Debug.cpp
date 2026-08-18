/*
 * Written by Adam Phelps, amp@cs.stanford.edu, 2014
 */

#include <Arduino.h>

#include "Debug.h"

char close_line = 0;

/*
 * Enter a persistent error state, flashing the indicated error code on the
 * built-in LED (pin 13);
 */
void debug_err_state(int code) {
  pinMode(13, OUTPUT);

  while (true) {
    /* Flash the error code */
    digitalWrite(13, LOW);
    for (int i = 0; i < code; i++) {
      delay(250);
      digitalWrite(13, HIGH);
      delay (250);
      digitalWrite(13, LOW);
    }

    /* Wait in between flashing the error code */
    delay(2000);
  }
}

/* Print the memory pointers and free space */
void debug_print_memory() {
#if defined(ESP32)
  /*
   * __brkval / __heap_start are avr-libc linker symbols and do not exist on
   * ESP32 -- referencing them made any DEBUG_LEVEL >= DEBUG_HIGH build that
   * calls DEBUG_MEMORY() fail to link (undefined reference to `__brkval').
   * The IDF heap API is the equivalent measure.
   */
  DEBUG1_VALUE("FREE HEAP: ", ESP.getFreeHeap());
  DEBUG1_VALUELN(" MIN FREE: ", ESP.getMinFreeHeap());
#elif defined(ESP8266)
  /* Same Arduino ESP class, but no getMinFreeHeap() on 8266. */
  DEBUG1_VALUELN("FREE HEAP: ", ESP.getFreeHeap());
#elif defined(__AVR__)
  extern int __heap_start, *__brkval;
  int v;

  v = (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);

  DEBUG1_VALUE("BRKVAL: ", ((int)__brkval));
  DEBUG1_VALUE(" HEAP_START: ", __heap_start);
  DEBUG1_VALUE(" &HEAP_START: ", (int)&__heap_start);
  DEBUG1_VALUE(" &V: ", (int)&v);
  DEBUG1_VALUELN(" FREE: ", v);
#else
  DEBUG1_PRINTLN("debug_print_memory: unsupported platform");
#endif
}

/*
 * Print the bytes of a buffer as hex values
 */
void print_hex_buffer(const char *buff, int length) {
  for (int b = 0; b < length; b++) {
    DEBUG4_HEXVAL( " ", buff[b]);
  }
}