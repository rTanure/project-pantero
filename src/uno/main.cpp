#include <Arduino.h>
#include <SoftwareSerial.h>
#include <avr/wdt.h>
#include <QC3Control.h>

// UART com o ESP32 (cruzado: RX do Uno <- TX do ESP32 e vice-versa)
const uint8_t PIN_RX = 12;
const uint8_t PIN_TX = 13;

// Sensores do seguidor de linha (TCRT5000)
const uint8_t PIN_LINE_L = A0;
const uint8_t PIN_LINE_R = A1;

// Quick Charge 3 (D+ / D-)
const uint8_t PIN_QC_DP = A3;
const uint8_t PIN_QC_DM = A2;

// Ponte H (todos PWM no Uno)
const uint8_t PIN_IN1 = 6;   // motor esquerdo
const uint8_t PIN_IN2 = 9;
const uint8_t PIN_IN3 = 10;  // motor direito
const uint8_t PIN_IN4 = 11;

// Velocidade máxima dos motores em % do PWM total (reduz consumo e picos de corrente)
#ifndef MAX_SPEED_PERCENT
#define MAX_SPEED_PERCENT 30
#endif

const unsigned long COMMAND_TIMEOUT_MS = 500;  // sem comando -> para o robô

// Motivo do reset: o MCUSR é lido e zerado em .init3 (antes do main), senão o watchdog pode
// ficar ativo e reiniciar o Uno em loop. Se o bootloader já tiver zerado o registrador, o valor é 0.
uint8_t mcusrMirror __attribute__((section(".noinit")));
void captureMcusr(void) __attribute__((naked)) __attribute__((used)) __attribute__((section(".init3")));
void captureMcusr(void) {
  mcusrMirror = MCUSR;
  MCUSR = 0;
  wdt_disable();
}

SoftwareSerial link(PIN_RX, PIN_TX);
QC3Control quickCharge(PIN_QC_DP, PIN_QC_DM);

const long DEFAULT_QC_MV = 9000;  // tensão padrão ao ligar
const unsigned long QC_REASSERT_MS = 5000;
const unsigned long QC_AFTER_BEGIN_DELAY_MS = 500;  // pausa entre o handshake e o 1º pedido de tensão

long targetMv = DEFAULT_QC_MV;
unsigned long lastQcAt = 0;
unsigned long lastCommandAt = 0;
bool stoppedByTimeout = true;

// Log no Serial USB (115200): "[ms][TAG] mensagem"
#define LOGU(tag, ...) do { Serial.print('['); Serial.print(millis()); Serial.print(F(" ms]["));  \
  Serial.print(F(tag)); Serial.print(F("] ")); Serial.println(__VA_ARGS__); } while (0)

// speed: -255..255 (sinal = sentido)
void driveMotor(uint8_t pinFwd, uint8_t pinBack, int speed) {
  speed = constrain(speed, -255, 255);
  if (speed >= 0) {
    analogWrite(pinFwd, speed);
    analogWrite(pinBack, 0);
  } else {
    analogWrite(pinFwd, 0);
    analogWrite(pinBack, -speed);
  }
}

// x: -100..100 (direita +), y: -100..100 (frente +). Mixagem estilo tanque.
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
  // 100% do comando = MAX_SPEED_PERCENT do PWM (o joystick continua usando todo o curso)
  left = left * MAX_SPEED_PERCENT / 100;
  right = right * MAX_SPEED_PERCENT / 100;
  driveMotor(PIN_IN1, PIN_IN2, left * 255 / 100);
  driveMotor(PIN_IN3, PIN_IN4, right * 255 / 100);

  // Só loga quando o movimento muda (evita inundar o serial a 12 msgs/s)
  static int lastL = 9999, lastR = 9999;
  if (left != lastL || right != lastR) {
    lastL = left;
    lastR = right;
    Serial.print('['); Serial.print(millis()); Serial.print(F(" ms][MOTOR] esq="));
    Serial.print(left); Serial.print(F("% dir=")); Serial.print(right); Serial.println('%');
  }
}

// Protocolo (linhas terminadas em \n):
//   M <x> <y>   mover, valores de -100 a 100
//   S           parar
//   V <mV>      tensão do Quick Charge: 5000/9000/12000 (discreto) ou 3600-12000 (contínuo QC3)
const char *resetReasonName(uint8_t m) {
  if (m & _BV(PORF)) return "POWERON";
  if (m & _BV(BORF)) return "BROWNOUT";
  if (m & _BV(WDRF)) return "WATCHDOG";
  if (m & _BV(EXTRF)) return "EXTERNAL";
  return "BOOTLOADER";  // MCUSR zerado pelo bootloader: causa desconhecida
}

// Envia ao ESP32 (que repassa ao app): "R,<MCUSR>,<NOME>,<uptime_ms>"
void sendResetInfo() {
  char msg[40];
  snprintf(msg, sizeof(msg), "R,%u,%s,%lu", mcusrMirror, resetReasonName(mcusrMirror), millis());
  link.println(msg);
  LOGU("RESET", String(F("enviado ao ESP32: ")) + msg);
}

// 5/9/12 V usam os pedidos discretos (QC2), que funcionam em qualquer carregador QC;
// só os valores intermediários usam o modo contínuo do QC3
void applyVoltage(long mv) {
  if (mv == 5000) quickCharge.set5V();
  else if (mv == 9000) quickCharge.set9V();
  else if (mv == 12000) quickCharge.set12V();
  else quickCharge.setMilliVoltage((unsigned int)mv);
  lastQcAt = millis();
}

void handleLine(char *line) {
  char cmd = line[0];
  if (cmd == 'M') {
    int x = 0, y = 0;
    if (sscanf(line + 1, "%d %d", &x, &y) == 2) {
      drive(x, y);
      lastCommandAt = millis();
      stoppedByTimeout = false;
    } else {
      LOGU("UART", String(F("M invalido: ")) + line);
    }
  } else if (cmd == 'S') {
    LOGU("UART", F("RX: S (parar)"));
    drive(0, 0);
    lastCommandAt = millis();
  } else if (cmd == '?') {
    sendResetInfo();  // o ESP32 pede o motivo do reset
  } else if (cmd == 'V') {
    long mv = atol(line + 1);
    if (mv >= 3600 && mv <= 12000) {
      targetMv = mv;
      applyVoltage(mv);
      LOGU("QC3", String(F("pedido enviado: ")) + mv + F(" mV"));
    } else {
      LOGU("QC3", String(F("tensao fora da faixa 3600-12000: ")) + mv);
    }
  } else {
    LOGU("UART", String(F("comando desconhecido: ")) + line);
  }
}

// Lê linhas de qualquer stream (UART do ESP32 ou Serial USB para testes manuais)
struct LineReader {
  char buf[24];
  uint8_t len = 0;
  void poll(Stream &in) {
    while (in.available()) {
      char c = in.read();
      if (c == '\n') {
        buf[len] = '\0';
        if (len > 0) handleLine(buf);
        len = 0;
      } else if (c != '\r' && len < sizeof(buf) - 1) {
        buf[len++] = c;
      }
    }
  }
};
LineReader linkReader, usbReader;

void setup() {
  Serial.begin(115200);
  LOGU("BOOT", F("Pantero Uno iniciando"));
  LOGU("RESET", String(F("MCUSR=")) + mcusrMirror + F(" (") + resetReasonName(mcusrMirror) + F(")"));
  link.begin(9600);  // SoftwareSerial é instável acima disso

  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_IN3, OUTPUT);
  pinMode(PIN_IN4, OUTPUT);
  pinMode(PIN_LINE_L, INPUT);
  pinMode(PIN_LINE_R, INPUT);
  drive(0, 0);

  LOGU("QC3", F("handshake: aguardando 1,5 s desde o boot (D+ em 0,6 V)..."));
  quickCharge.begin();
  LOGU("QC3", String(F("handshake concluido, aguardando ")) + QC_AFTER_BEGIN_DELAY_MS + F(" ms antes do pedido"));
  delay(QC_AFTER_BEGIN_DELAY_MS);
  LOGU("QC3", F("pedindo 9 V (padrao)"));
  applyVoltage(targetMv);
  LOGU("QC3", String(F("pedido enviado: ")) + quickCharge.getMilliVoltage()
                  + F(" mV (valor PEDIDO, nao medido: confira com multimetro no VBUS)"));
  sendResetInfo();  // avisa o ESP32 que o Uno (re)iniciou
  LOGU("BOOT", String(F("velocidade maxima: ")) + MAX_SPEED_PERCENT + F("% do PWM"));
  LOGU("BOOT", F("pronto, aguardando comandos do ESP32 (SoftwareSerial 9600, RX=12 TX=13)"));
}

void loop() {
  linkReader.poll(link);    // comandos do ESP32
  usbReader.poll(Serial);   // comandos digitados no monitor serial (ex.: V 9000)
  if (millis() - lastCommandAt > COMMAND_TIMEOUT_MS) {
    drive(0, 0);
    if (!stoppedByTimeout) {
      stoppedByTimeout = true;
      LOGU("SAFE", F("sem comandos ha 500 ms, motores parados (watchdog)"));
    }
  }

  // Reafirma a tensão-alvo (9 V por padrão) enquanto o robô está parado: se o carregador
  // perdeu o primeiro pedido ou voltou para 5 V, ele é pedido de novo. O pedido bloqueia ~60 ms,
  // por isso não é feito durante a condução.
  if (stoppedByTimeout && millis() - lastQcAt >= QC_REASSERT_MS) {
    applyVoltage(targetMv);
    LOGU("QC3", String(F("pedido reafirmado: ")) + targetMv + F(" mV"));
  }

  // Heartbeat a cada 10 s com os sensores de linha (útil para calibrar o TCRT5000)
  static unsigned long lastBeat = 0;
  if (millis() - lastBeat >= 10000) {
    lastBeat = millis();
    Serial.print('['); Serial.print(millis()); Serial.print(F(" ms][BEAT] linha esq="));
    Serial.print(analogRead(PIN_LINE_L)); Serial.print(F(" dir="));
    Serial.println(analogRead(PIN_LINE_R));
  }
}
