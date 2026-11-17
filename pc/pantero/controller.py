"""Controlador de compressão adaptativa, guiado pelo que o PC realmente recebe.

Quando há perda de quadros ou o atraso sobe, aumenta a compressão (qualidade JPEG pior) e, se não
bastar, reduz a resolução. Quando a rede folga por um tempo, volta a melhorar. O sinal vem do receptor
(quadros incompletos, atraso relativo, fps recebido contra enviado), e não do tempo de envio no ESP32,
que se mostrou um indicador ruim.
"""
from typing import Optional

from .settings import Settings

# Escada de (tamanho da imagem, qualidade JPEG): índice maior = menos bytes por quadro
LEVELS = [(2, 12), (2, 15), (2, 18), (2, 22), (2, 28), (2, 36), (1, 24), (1, 34), (0, 24), (0, 36)]

LOSS_BAD_PCT = 8.0
LOSS_GOOD_PCT = 2.0
LATENCY_BAD_MS = 250.0
LATENCY_GOOD_MS = 120.0
BAD_CYCLES = 2        # ciclos ruins seguidos para piorar a qualidade (reage rápido)
GOOD_CYCLES = 6       # ciclos bons seguidos para melhorar (sobe devagar)
MIN_HOLD_S = 6.0      # intervalo mínimo entre uma melhora e a mudança anterior


class AdaptiveController:
    def __init__(self, settings: Settings):
        self.settings = settings
        self._bad = 0
        self._good = 0
        self._last_change = -1e9
        self.level = self._nearest_level()

    def _nearest_level(self) -> int:
        best = min(range(len(LEVELS)), key=lambda i: (LEVELS[i][0] != self.settings.frame_size,
                                                      abs(LEVELS[i][1] - self.settings.quality)))
        return best

    def update(self, loss_pct: float, latency_p95_ms: Optional[float], recv_fps: float, sent_fps: float,
               now: float) -> Optional[str]:
        """Chamar uma vez por segundo. Muda settings e devolve a descrição, se houve mudança."""
        has_sent = sent_fps > 5
        bad = (loss_pct > LOSS_BAD_PCT
               or (latency_p95_ms is not None and latency_p95_ms > LATENCY_BAD_MS)
               or (has_sent and recv_fps < 0.6 * sent_fps))
        good = (loss_pct < LOSS_GOOD_PCT
                and (latency_p95_ms is None or latency_p95_ms < LATENCY_GOOD_MS)
                and (not has_sent or recv_fps >= 0.9 * sent_fps))
        self._bad = self._bad + 1 if bad else 0
        self._good = self._good + 1 if good else 0

        if self._bad >= BAD_CYCLES and self.level < len(LEVELS) - 1:
            return self._move(self.level + 1, now, f"rede ruim (perda {loss_pct:.0f}%, atraso p95 {latency_p95_ms or 0:.0f} ms)")
        if self._good >= GOOD_CYCLES and self.level > 0 and now - self._last_change >= MIN_HOLD_S:
            return self._move(self.level - 1, now, "rede folgada")
        return None

    def _move(self, level: int, now: float, reason: str) -> str:
        previous = self.level
        self.level = level
        self._bad = self._good = 0
        self._last_change = now
        self.settings.frame_size, self.settings.quality = LEVELS[level]
        return f"nivel {previous} -> {level}: {reason}"
