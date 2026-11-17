#pragma once
#include <Arduino.h>

// Log no Serial USB (115200): "[  1234 ms][TAG] mensagem". O tag e as mensagens fixas ficam na flash (F()).
#define LOGU(tag, ...)                \
  do {                                \
    Serial.print('[');                \
    Serial.print(millis());           \
    Serial.print(F(" ms]["));         \
    Serial.print(F(tag));             \
    Serial.print(F("] "));            \
    Serial.println(__VA_ARGS__);      \
  } while (0)
