"""Avisos de reset enviados pelo ESP32 pelo link UDP:
  "R,<codigo>,<NOME>,<uptime_ms>"  reset do ESP32
  "U,<codigo>,<NOME>,<uptime_ms>"  reset do Arduino Uno (repassado pelo ESP32)
O ESP32 repete o aviso a cada "olá" (1 s); só interessa quando muda ou quando a placa reiniciou.
"""
from dataclasses import dataclass
from typing import Optional

BOARDS = {"R": ("esp", "ESP32"), "U": ("uno", "Arduino")}

# Motivos que indicam problema (alimentação fraca, travamento, etc.)
ABNORMAL = {"BROWNOUT", "PANIC", "INT_WDT", "TASK_WDT", "WDT", "WATCHDOG", "UNKNOWN"}

RECENT_MS = 60_000          # reset anormal só é destacado se a placa acabou de ligar
REBOOT_TOLERANCE_MS = 2_000  # tolerância ao comparar o uptime recebido com o esperado


@dataclass
class ResetInfo:
    board: str       # "esp" ou "uno"
    label: str       # "ESP32" ou "Arduino"
    code: int
    name: str
    uptime_ms: int
    abnormal: bool


def parse_reset_message(text: str) -> Optional[ResetInfo]:
    """Converte o texto em ResetInfo, ou None se não for um aviso de reset."""
    if len(text) < 2 or text[0] not in BOARDS or text[1] != ",":
        return None
    parts = text.split(",")
    if len(parts) != 4:
        return None
    try:
        code, uptime = int(parts[1]), int(parts[3])
    except ValueError:
        return None
    board, label = BOARDS[text[0]]
    return ResetInfo(board, label, code, parts[2], uptime, parts[2] in ABNORMAL)


def detect_reboot(previous: Optional[tuple], uptime_ms: int, now_ms: float) -> bool:
    """A placa reiniciou se o uptime atual é menor que o esperado desde a última mensagem.
    previous = (uptime_ms, recebido_em_ms) da mensagem anterior, ou None na primeira."""
    if previous is None:
        return False
    prev_uptime, prev_at = previous
    return uptime_ms < prev_uptime + (now_ms - prev_at) - REBOOT_TOLERANCE_MS


def should_alert(info: ResetInfo, rebooted: bool) -> bool:
    """Só vale avisar se o reset foi anormal e recente, ou se aconteceu com o programa aberto."""
    return info.abnormal and (rebooted or info.uptime_ms < RECENT_MS)


@dataclass
class Update:
    info: ResetInfo
    rebooted: bool
    changed: bool  # novidade em relação à mensagem anterior (a interface só registra se for True)
    alert: bool


class ResetTracker:
    """Acompanha o último aviso de cada placa."""

    def __init__(self):
        self._last = {}  # board -> (uptime_ms, recebido_em_ms)
        self._reason = {}  # board -> (code, name)

    def update(self, text: str, now_ms: float) -> Optional[Update]:
        info = parse_reset_message(text)
        if info is None:
            return None
        rebooted = detect_reboot(self._last.get(info.board), info.uptime_ms, now_ms)
        first = info.board not in self._last
        changed = first or rebooted or self._reason.get(info.board) != (info.code, info.name)
        self._last[info.board] = (info.uptime_ms, now_ms)
        self._reason[info.board] = (info.code, info.name)
        return Update(info, rebooted, changed, should_alert(info, rebooted))
