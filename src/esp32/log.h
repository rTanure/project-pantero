#pragma once
#include <Arduino.h>

// Log de debug no Serial USB: "[  12345 ms][TAG] mensagem"
// Para silenciar tudo: build_flags = -DPANTERO_LOG=0
#ifndef PANTERO_LOG
#define PANTERO_LOG 1
#endif

#if PANTERO_LOG
#define LOG(tag, fmt, ...) Serial.printf("[%8lu ms][%s] " fmt "\n", millis(), tag, ##__VA_ARGS__)
#else
#define LOG(tag, fmt, ...) ((void)0)
#endif
