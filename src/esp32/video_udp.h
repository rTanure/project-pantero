#pragma once
#include <stddef.h>

// Vídeo da câmera por UDP, sem confiabilidade: perdeu, perdeu.
// Cada quadro JPEG é dividido em pacotes de VIDEO_CHUNK_BYTES, enviados para o PC registrado no
// udp_link. Pacote = cabeçalho de 8 bytes + dados:
//   byte 0-1  id do quadro (16 bits, big-endian, dá a volta)
//   byte 2    índice deste pedaço (0 a total-1; índice == total é o pacote de paridade FEC)
//   byte 3    total de pedaços de dados do quadro
//   byte 4-7  instante da captura em ms (millis do ESP32, 32 bits, big-endian): o PC mede o atraso
// Pacote de paridade (FEC, opcional): 4 bytes com o tamanho total do JPEG + o XOR dos pedaços
// (o último preenchido com zeros). Com ele o PC recupera um pedaço perdido do quadro.
// Uma task no núcleo 1 captura e envia, e só roda enquanto houver PC conectado.
//
// Ajustes (enviados pelo PC no "olá", ver udp_link.h): q (10-63 qualidade JPEG, maior = mais comprimido),
// fs (0 = 160x120, 1 = 240x176, 2 = 320x240), fec (0/1), pace (µs entre pacotes; padrão 4000), cam (0 = 1 buffer
// que só captura com o buffer livre, 1 = 2 buffers com o quadro mais recente).
namespace video_udp {

struct Info {
  float fps;            // quadros por segundo enviados (média dos últimos ~2 s)
  int quality, frameSize, fec, paceUs, camMode;
  unsigned long lostPackets;  // pacotes que o rádio não aceitou desde o boot
};

void begin();                        // inicia a task de captura e envio
bool set(const char *key, int value);  // aplica um ajuste; false se a chave for desconhecida
Info info();
float fps();

}  // namespace video_udp
