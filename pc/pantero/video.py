"""Receptor de vídeo por UDP, com remontagem, FEC e métricas.

Cada quadro JPEG chega dividido em pacotes: 8 bytes de cabeçalho + dados.
  byte 0-1  id do quadro (16 bits, big-endian, dá a volta)
  byte 2    índice do pedaço (0 a total-1; índice == total é o pacote de paridade FEC)
  byte 3    total de pedaços de dados do quadro
  byte 4-7  instante da captura em ms (relógio do ESP32): serve para medir o atraso
Pacote de paridade: 4 bytes com o tamanho total do JPEG + XOR dos pedaços (o último preenchido com zeros).
Sem retransmissão: quadro com pedaço faltando (e sem como recuperar) é descartado.
"""
import collections
import io
import socket
import struct
import threading
import time
from dataclasses import dataclass
from typing import NamedTuple, Optional, Tuple

from PIL import Image

from . import config
from .log import log

HEADER = struct.Struct(">HBBI")
STATS_WINDOW_S = 5.0     # janela das métricas
LATENCY_BASE_S = 10.0    # janela do "melhor caso" usado como referência do atraso


class Frame(NamedTuple):
    jpeg: bytes
    ts_ms: int  # instante da captura no relógio do ESP32


def _is_newer(a: int, b: int) -> bool:
    """a é mais novo que b, considerando que o id de 16 bits dá a volta."""
    return a != b and ((a - b) & 0xFFFF) < 0x8000


def _xor(a: bytearray, b: bytes) -> None:
    for i, byte in enumerate(b):
        a[i] ^= byte


class FrameAssembler:
    """Remonta quadros a partir dos pacotes, tolerando perda, duplicação e desordem dentro do quadro.
    Com FEC, recupera um pedaço perdido por quadro."""

    def __init__(self):
        self.frame_id: Optional[int] = None
        self.count = 0
        self.ts_ms = 0
        self.chunks = {}
        self.parity: Optional[bytes] = None
        self.completed_id: Optional[int] = None
        self.dropped = 0    # quadros descartados por ficarem incompletos
        self.recovered = 0  # quadros que fecharam graças à paridade

    def feed(self, packet: bytes) -> Optional[Frame]:
        """Processa um pacote. Devolve o quadro quando ele fecha, senão None."""
        if len(packet) <= HEADER.size:
            return None
        frame_id, index, count, ts_ms = HEADER.unpack_from(packet)
        if count == 0 or index > count or frame_id == self.completed_id:
            return None

        if frame_id != self.frame_id:
            if self.frame_id is not None and not _is_newer(frame_id, self.frame_id):
                return None  # pacote atrasado de um quadro velho
            if self.frame_id is not None and (self.chunks or self.parity):
                self.dropped += 1  # o quadro anterior ficou incompleto
            self.frame_id, self.count, self.ts_ms, self.chunks, self.parity = frame_id, count, ts_ms, {}, None
        if count != self.count:
            return None

        payload = packet[HEADER.size:]
        if index == count:
            self.parity = payload
        else:
            self.chunks[index] = payload
        return self._try_complete()

    def _try_complete(self) -> Optional[Frame]:
        have = len(self.chunks)
        if have == self.count:
            data = b"".join(self.chunks[i] for i in range(self.count))
        elif have == self.count - 1 and self.parity is not None and len(self.parity) > 4:
            data = self._recover()
            if data is None:
                return None
            self.recovered += 1
        else:
            return None
        self.completed_id = self.frame_id
        ts_ms = self.ts_ms
        self.chunks, self.parity = {}, None
        return Frame(data, ts_ms)

    def _recover(self) -> Optional[bytes]:
        """Reconstrói o pedaço que falta com o XOR dos outros e o da paridade."""
        total_len = int.from_bytes(self.parity[:4], "big")
        chunk_size = len(self.parity) - 4
        missing = next(i for i in range(self.count) if i not in self.chunks)
        rebuilt = bytearray(self.parity[4:])
        for chunk in self.chunks.values():
            _xor(rebuilt, chunk)
        if missing == self.count - 1:  # o último pedaço é mais curto
            size = total_len - (self.count - 1) * chunk_size
            if not 0 < size <= chunk_size:
                return None
            rebuilt = rebuilt[:size]
        chunks = dict(self.chunks)
        chunks[missing] = bytes(rebuilt)
        return b"".join(chunks[i] for i in range(self.count))


@dataclass
class VideoStats:
    fps: float = 0.0
    bitrate_mbps: float = 0.0
    frame_kb: float = 0.0
    latency_p50_ms: Optional[float] = None  # atraso em relação ao melhor caso recente (fila, jitter)
    latency_p95_ms: Optional[float] = None
    jitter_p95_ms: Optional[float] = None   # intervalo entre quadros
    loss_pct: float = 0.0                   # quadros descartados / quadros esperados, na janela
    recovered: int = 0                      # total de quadros salvos pela paridade
    dropped: int = 0                        # total de quadros descartados


def _percentile(values, pct: float) -> Optional[float]:
    if not values:
        return None
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(len(ordered) * pct / 100))]


class VideoReceiver:
    def __init__(self, port: int = config.VIDEO_PORT):
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
        except OSError:
            pass
        self._sock.bind(("", port))
        self._port = self._sock.getsockname()[1]  # porta real (útil quando port=0)
        self._sock.settimeout(0.2)
        self._assembler = FrameAssembler()
        self._lock = threading.Lock()
        self._image: Optional[Image.Image] = None
        self._seq = 0
        self._frames = collections.deque()   # (chegada_s, ts_ms, bytes do JPEG) dos quadros recentes
        self._bytes = collections.deque()    # (chegada_s, bytes) de cada pacote, para o bitrate
        self._events = collections.deque()   # (chegada_s, completou?) para a taxa de perda
        self._last_frame_at = 0.0
        self._running = False
        self._thread: Optional[threading.Thread] = None

    @property
    def port(self) -> int:
        return self._port

    @property
    def dropped(self) -> int:
        return self._assembler.dropped

    @property
    def stale(self) -> bool:
        """Sem quadro novo há um tempo (robô desligado, fora de alcance ou sem câmera)."""
        return time.monotonic() - self._last_frame_at > config.VIDEO_STALE_S

    def latest(self) -> Tuple[int, Optional[Image.Image]]:
        """(número do quadro, imagem). O número muda a cada quadro novo."""
        with self._lock:
            return self._seq, self._image

    def stats(self) -> VideoStats:
        """Métricas dos últimos segundos."""
        now = time.monotonic()
        with self._lock:
            self._trim(now)
            frames = list(self._frames)
            packets = list(self._bytes)
            events = list(self._events)
        recent = [f for f in frames if now - f[0] <= STATS_WINDOW_S]

        stats = VideoStats(recovered=self._assembler.recovered, dropped=self._assembler.dropped)
        if len(recent) >= 2:
            span = max(recent[-1][0] - recent[0][0], 1e-6)
            stats.fps = (len(recent) - 1) / span
            stats.frame_kb = sum(f[2] for f in recent) / len(recent) / 1024
            stats.jitter_p95_ms = _percentile([(b[0] - a[0]) * 1000 for a, b in zip(recent, recent[1:])], 95)
            # atraso relativo: (chegada - captura) menos o menor valor recente (o melhor caso)
            offsets = [f[0] * 1000 - f[1] for f in frames]
            base = min(offsets)
            rel = [f[0] * 1000 - f[1] - base for f in recent]
            stats.latency_p50_ms = _percentile(rel, 50)
            stats.latency_p95_ms = _percentile(rel, 95)
        window_bytes = sum(p[1] for p in packets if now - p[0] <= STATS_WINDOW_S)
        stats.bitrate_mbps = window_bytes * 8 / STATS_WINDOW_S / 1e6
        done = sum(1 for e in events if e[1])
        lost = len(events) - done
        stats.loss_pct = 100.0 * lost / len(events) if events else 0.0
        return stats

    def reset_metrics(self) -> None:
        with self._lock:
            self._frames.clear()
            self._bytes.clear()
            self._events.clear()

    def start(self) -> None:
        self._running = True
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()
        log.info("VIDEO", f"recebendo video na porta UDP {self._port}")

    def stop(self) -> None:
        self._running = False
        if self._thread:
            self._thread.join(timeout=1)
        self._sock.close()

    def _trim(self, now: float) -> None:
        for queue_, keep in ((self._frames, LATENCY_BASE_S), (self._bytes, STATS_WINDOW_S),
                             (self._events, STATS_WINDOW_S)):
            while queue_ and now - queue_[0][0] > keep:
                queue_.popleft()

    def _loop(self) -> None:
        while self._running:
            try:
                packet, _ = self._sock.recvfrom(65535)
            except socket.timeout:
                continue
            except OSError:
                return
            now = time.monotonic()
            dropped_before = self._assembler.dropped
            with self._lock:
                self._bytes.append((now, len(packet)))
            frame = self._assembler.feed(packet)
            for _ in range(self._assembler.dropped - dropped_before):
                with self._lock:
                    self._events.append((now, False))
            if frame is None:
                continue
            try:
                image = Image.open(io.BytesIO(frame.jpeg))
                image.load()
            except Exception as err:  # JPEG corrompido
                log.warn("VIDEO", f"quadro invalido descartado: {err}")
                continue
            with self._lock:
                self._image = image
                self._seq += 1
                self._frames.append((now, frame.ts_ms, len(frame.jpeg)))
                self._events.append((now, True))
                self._last_frame_at = now
