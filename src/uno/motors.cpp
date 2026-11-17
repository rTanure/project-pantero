#include "motors.h"
#include "config.h"
#include "log.h"

namespace motors {
namespace {

bool active = false;  // há comando recente em andamento
unsigned long lastCommandAt = 0;

// speed: -255..255 (sinal = sentido)
void setMotor(uint8_t pinForward, uint8_t pinBackward, int speed) {
  speed = constrain(speed, -255, 255);
  if (speed >= 0) {
    analogWrite(pinForward, speed);
    analogWrite(pinBackward, 0);
  } else {
    analogWrite(pinForward, 0);
    analogWrite(pinBackward, -speed);
  }
}

// Só loga quando o movimento muda (evita inundar o serial a ~12 mensagens por segundo)
void logIfChanged(int left, int right) {
  static int lastLeft = 9999, lastRight = 9999;
  if (left == lastLeft && right == lastRight) return;
  lastLeft = left;
  lastRight = right;
  Serial.print('[');
  Serial.print(millis());
  Serial.print(F(" ms][MOTOR] esq="));
  Serial.print(left);
  Serial.print(F("% dir="));
  Serial.print(right);
  Serial.println('%');
}

}  // namespace

void begin() {
  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_IN3, OUTPUT);
  pinMode(PIN_IN4, OUTPUT);
  stop();
}

void drive(int x, int y) {
  x = constrain(x, -100, 100);
  y = constrain(y, -100, 100);
  int left = y + x;
  int right = y - x;
  int peak = max(abs(left), abs(right));
  if (peak > 100) {  // mantém a proporção quando passa de 100%
    left = left * 100 / peak;
    right = right * 100 / peak;
  }
  left = left * MAX_SPEED_PERCENT / 100;
  right = right * MAX_SPEED_PERCENT / 100;
  setMotor(PIN_IN1, PIN_IN2, left * 255 / 100);
  setMotor(PIN_IN3, PIN_IN4, right * 255 / 100);

  active = true;
  lastCommandAt = millis();
  logIfChanged(left, right);
}

void stop() {
  setMotor(PIN_IN1, PIN_IN2, 0);
  setMotor(PIN_IN3, PIN_IN4, 0);
  active = false;
  logIfChanged(0, 0);
}

void update() {
  if (active && millis() - lastCommandAt > COMMAND_TIMEOUT_MS) {
    stop();
    LOGU("SAFE", F("sem comandos, motores parados (watchdog)"));
  }
}

bool isIdle() { return !active; }

}  // namespace motors
