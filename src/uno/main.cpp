#include <Arduino.h>

void setup() {
  Serial.begin(115200);
}

void loop() {
  Serial.println("Hello world do Arduino Uno!");
  delay(1000);
}