"""Log central: imprime no terminal e guarda as últimas linhas para o painel da interface."""
import collections
import threading
import time
from typing import NamedTuple


class Line(NamedTuple):
    seq: int
    time: str
    level: str  # info | warn | error
    tag: str
    text: str


class Logger:
    def __init__(self, max_lines: int = 300):
        self._lines = collections.deque(maxlen=max_lines)
        self._lock = threading.Lock()
        self._seq = 0

    def _push(self, level: str, tag: str, message: str) -> None:
        now = time.time()
        stamp = time.strftime("%H:%M:%S", time.localtime(now)) + f".{int(now * 1000) % 1000:03d}"
        with self._lock:
            self._seq += 1
            self._lines.append(Line(self._seq, stamp, level, tag, message))
        print(f"[{stamp}][{tag}] {message}", flush=True)

    def info(self, tag: str, message: str) -> None:
        self._push("info", tag, message)

    def warn(self, tag: str, message: str) -> None:
        self._push("warn", tag, message)

    def error(self, tag: str, message: str) -> None:
        self._push("error", tag, message)

    def since(self, seq: int) -> list:
        """Linhas mais novas que `seq` (a interface chama de tempos em tempos)."""
        with self._lock:
            return [line for line in self._lines if line.seq > seq]


log = Logger()
