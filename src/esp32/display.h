#pragma once

// Painel OLED SSD1306 128x64 (I2C, SDA 32 / SCL 33) com o status do robô.
// O painel está girado 90°, então a tela lógica é em retrato (64x128 px = 10 colunas x 16 linhas).
// Se o painel não responder no I2C, o módulo se desativa e o resto do firmware segue normal.
namespace display {

void begin();                // inicia o I2C e mostra a tela de partida
void setCameraOk(bool ok);   // resultado do init da câmera (mostrado na linha de vídeo)
// (a tela é redesenhada a cada DISPLAY_REFRESH_MS por uma task própria: o I2C não bloqueia o loop())

}  // namespace display
