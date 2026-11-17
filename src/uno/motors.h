#pragma once

// Motores DC na ponte H, com mistura estilo tanque e parada automática por falta de comando.
namespace motors {

void begin();
void drive(int x, int y);  // x: direita +, y: frente + (-100..100); 100% = MAX_SPEED_PERCENT do PWM
void stop();
void update();             // chamar no loop(): para o robô se ficar sem comando por COMMAND_TIMEOUT_MS
bool isIdle();             // true quando os motores estão parados (sem comando ativo)

}  // namespace motors
