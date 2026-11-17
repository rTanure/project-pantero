"""Enlace de controle com o ESP32 por UDP.

PC -> ESP32 (porta CONTROL_PORT):  "H,<porta de video>,<ajustes>" a cada ~1 s (olá/keep-alive; os ajustes são
                                   reaplicados a cada olá), "x,y" (comando) e "P,<n>" (ping)
ESP32 -> PC:                       "R,..." e "U,..." (avisos de reset), "S,..." (estado do vídeo) e "P,<n>" (eco)
Qualquer resposta confirma que o ESP32 vive.
"""
import collections
import socket
import threading
import time
from typing import Callable, Dict, Optional, Tuple

from . import config
from .log import log
from .settings import Settings

PING_INTERVAL_S = 0.5
RTT_WINDOW = 40  # últimas medições usadas nas estatísticas


class RobotLink:
    def __init__(
        self,
        read_command: Callable[[], Tuple[int, int]],
        on_message: Callable[[str], None],
        settings: Optional[Settings] = None,
        robot_ip: str = config.ROBOT_IP,
        control_port: int = config.CONTROL_PORT,
        video_port: int = config.VIDEO_PORT,
    ):
        self._read_command = read_command  # chamado pela thread de envio: devolve (x, y) de -100 a 100
        self._on_message = on_message      # chamado pela thread de recepção, uma vez por linha de aviso (R, U)
        self.settings = settings or Settings()
        self._target = (robot_ip, control_port)
        self._video_port = video_port
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.settimeout(0.2)
        self._running = False
        self._threads = []
        self._last_rx = 0.0
        self._last_sent = ""
        self._send_failed = False
        self._was_connected = False
        self._ping_seq = 0
        self._ping_sent: Dict[int, float] = {}
        self._rtts = collections.deque(maxlen=RTT_WINDOW)  # ida e volta do canal de controle, em ms
        self.esp_status: Dict[str, float] = {}             # último "S,..." recebido

    @property
    def connected(self) -> bool:
        """O ESP32 respondeu há pouco tempo."""
        return self._last_rx > 0 and time.monotonic() - self._last_rx < config.LINK_TIMEOUT_S

    @property
    def last_command(self) -> str:
        return self._last_sent or "0,0"

    def rtt_stats(self) -> Tuple[Optional[float], Optional[float]]:
        """(média, p95) do tempo de ida e volta do controle, em ms."""
        values = sorted(self._rtts)
        if not values:
            return None, None
        return sum(values) / len(values), values[min(len(values) - 1, int(len(values) * 0.95))]

    def start(self) -> None:
        self._running = True
        for target in (self._tx_loop, self._rx_loop):
            thread = threading.Thread(target=target, daemon=True)
            thread.start()
            self._threads.append(thread)
        log.info("LINK", f"enviando para {self._target[0]}:{self._target[1]} (video na porta local {self._video_port})")

    def stop(self) -> None:
        self._running = False
        for thread in self._threads:
            thread.join(timeout=1)
        for _ in range(3):  # UDP pode perder: manda a parada algumas vezes (o ESP32 e o Uno também param sozinhos)
            self._send("0,0")
        self._sock.close()

    # ------------------------------------------------------------------ interno
    def _send(self, message: str) -> None:
        try:
            self._sock.sendto(message.encode(), self._target)
            self._send_failed = False
        except OSError as err:
            if not self._send_failed:  # avisa uma vez só (ex.: PC fora do Wi-Fi do robô)
                log.warn("LINK", f"falha ao enviar: {err}")
            self._send_failed = True

    def _tx_loop(self) -> None:
        next_hello = next_ping = 0.0
        while self._running:
            now = time.monotonic()
            if now >= next_hello:
                self._send(f"H,{self._video_port},{self.settings.hello_params()}")
                next_hello = now + config.HELLO_INTERVAL_S
            if now >= next_ping:
                self._ping_seq = (self._ping_seq + 1) % 100000
                self._ping_sent[self._ping_seq] = now
                if len(self._ping_sent) > 100:  # pings sem resposta: esquece os mais antigos
                    self._ping_sent.pop(next(iter(self._ping_sent)))
                self._send(f"P,{self._ping_seq}")
                next_ping = now + PING_INTERVAL_S

            x, y = self._read_command()
            message = f"{x},{y}"
            moving = x != 0 or y != 0
            if moving or message != self._last_sent:  # enquanto parado, só o 0,0 final
                self._send(message)
                if message != self._last_sent:
                    log.info("TX", f"x,y = {message}")
                self._last_sent = message

            connected = self.connected
            if connected != self._was_connected:
                self._was_connected = connected
                (log.info if connected else log.warn)("LINK", "ESP32 conectado" if connected else "ESP32 sem resposta")
            time.sleep(config.SEND_INTERVAL_S)

    def _rx_loop(self) -> None:
        while self._running:
            try:
                data, _ = self._sock.recvfrom(1024)
            except socket.timeout:
                continue
            except OSError:
                return
            now = time.monotonic()
            self._last_rx = now
            for line in data.decode(errors="replace").splitlines():
                if line.startswith("P,"):
                    self._handle_pong(line, now)
                elif line.startswith("S,"):
                    self._handle_status(line)
                elif line:
                    self._on_message(line)

    def _handle_pong(self, line: str, now: float) -> None:
        try:
            sent = self._ping_sent.pop(int(line[2:]), None)
        except ValueError:
            return
        if sent is not None:
            self._rtts.append((now - sent) * 1000)

    def _handle_status(self, line: str) -> None:
        """"S,<fps>,<q>,<fs>,<fec>,<pace>,<cam>,<rate>,<perdidos>" """
        parts = line.split(",")
        if len(parts) != 9:
            return
        try:
            values = [float(p) for p in parts[1:]]
        except ValueError:
            return
        keys = ("fps", "q", "fs", "fec", "pace", "cam", "rate", "lost")
        self.esp_status = dict(zip(keys, values))
