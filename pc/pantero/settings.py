"""Ajustes do vídeo e do rádio, enviados ao ESP32 junto do "olá" (a cada ~1 s)."""
from dataclasses import dataclass

# Tamanhos de imagem: índice -> (largura, altura)
FRAME_SIZES = {0: (160, 120), 1: (240, 176), 2: (320, 240)}
CAMERA_MODES = {0: "1 buffer (captura só com o buffer livre)", 1: "2 buffers (quadro mais recente)"}
WIFI_RATES = {0: "automática", 1: "fixa 54 Mbps", 2: "fixa MCS7 (72 Mbps)"}


@dataclass
class Settings:
    quality: int = 15       # qualidade JPEG: 10 (melhor) a 63 (mais comprimido)
    frame_size: int = 2     # ver FRAME_SIZES
    fec: bool = False       # pacote de paridade por quadro: recupera um pacote perdido
    pace_us: int = 4000     # espaço entre os pacotes de um quadro (evita rajada): reduziu o atraso p95 de ~160 para ~30 ms
    cam_mode: int = 0       # ver CAMERA_MODES
    wifi_rate: int = 0      # ver WIFI_RATES
    adaptive: bool = False  # o PC ajusta qualidade e resolução conforme a perda e o atraso

    def hello_params(self) -> str:
        """Ajustes no formato do olá: "q=15,fs=2,fec=0,pace=0,cam=0,rate=0"."""
        return (f"q={self.quality},fs={self.frame_size},fec={int(self.fec)},pace={self.pace_us},"
                f"cam={self.cam_mode},rate={self.wifi_rate}")

    def describe(self) -> str:
        width, height = FRAME_SIZES.get(self.frame_size, (0, 0))
        return (f"{width}x{height} q{self.quality} fec={'sim' if self.fec else 'nao'} pace={self.pace_us}us "
                f"cam={self.cam_mode} rate={self.wifi_rate}")
