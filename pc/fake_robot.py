#!/usr/bin/env python3
"""Simulador do ESP32 para testar o programa do PC sem o robô.

Fala o mesmo protocolo UDP do firmware (src/esp32/udp_link.h e video_udp.h): recebe "H,<porta>,<ajustes>",
comandos "x,y" e pings "P,<n>"; responde com avisos de reset e estado; transmite um vídeo sintético
fragmentado, com timestamp e FEC opcional.

  python3 pc/fake_robot.py                  # escuta na porta 4210 (use --ip 127.0.0.1 no main.py)
  python3 pc/fake_robot.py --loss 0.05      # descarta 5% dos pacotes de vídeo, para testar a perda
"""
import argparse
import io
import random
import socket
import struct
import threading
import time

from PIL import Image, ImageDraw

from pantero import config

CHUNK_BYTES = 1400          # igual ao VIDEO_CHUNK_BYTES do firmware
CLIENT_TIMEOUT_S = 3.0      # igual ao CLIENT_TIMEOUT_MS do firmware
SIZES = {0: (160, 120), 1: (240, 176), 2: (320, 240)}


def make_frame(counter: int, command: str, size=(320, 240), quality: int = 15) -> bytes:
    """JPEG sintético: fundo com gradiente, quadrado em movimento e contador. `quality` é o do ESP32 (10-63)."""
    width, height = size
    image = Image.new("RGB", size)
    draw = ImageDraw.Draw(image)
    for y in range(height):
        draw.line([(0, y), (width, y)], fill=(20 + y * 80 // height, 40, 90 - y * 60 // height))
    x = 10 + (counter * 6) % max(1, width - 60)
    draw.rectangle([x, height // 3, x + 50, height // 3 + 50], fill=(235, 104, 52))
    draw.text((8, 8), f"quadro {counter}", fill=(255, 255, 255))
    draw.text((8, 24), f"cmd {command}", fill=(255, 255, 255))
    out = io.BytesIO()
    image.save(out, "JPEG", quality=int(max(10, min(95, 100 - quality * 1.6))))
    return out.getvalue()


class FakeRobot:
    def __init__(self, control_port: int = config.CONTROL_PORT, bind: str = "127.0.0.1", fps: int = 15,
                 loss: float = 0.0):
        self._fps = fps
        self._loss = loss
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.bind((bind, control_port))
        self._sock.settimeout(0.1)
        self.control_port = self._sock.getsockname()[1]
        self._video = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._client = None          # (ip, porta de controle, porta de vídeo)
        self._last_rx = 0.0
        self._running = False
        self._started = time.monotonic()
        self.commands = []           # comandos "x,y" recebidos
        self.hellos = 0
        self.pings = 0
        self.frames_sent = 0
        self.stopped_by_timeout = 0
        self.settings = {"q": 15, "fs": 2, "fec": 0, "pace": 0, "cam": 0, "rate": 0}  # ajustes recebidos no olá
        self._random = random.Random(1)

    def _uptime_ms(self) -> int:
        return int((time.monotonic() - self._started) * 1000)

    def start(self) -> None:
        self._running = True
        for target in (self._control_loop, self._video_loop):
            threading.Thread(target=target, daemon=True).start()

    def stop(self) -> None:
        self._running = False
        time.sleep(0.25)
        self._sock.close()
        self._video.close()

    def _active(self) -> bool:
        return self._client is not None and time.monotonic() - self._last_rx < CLIENT_TIMEOUT_S

    def _reply(self, text: str) -> None:
        if self._client:
            self._sock.sendto(text.encode(), (self._client[0], self._client[1]))

    def _apply_settings(self, items) -> None:
        for item in items:
            key, _, value = item.partition("=")
            if key in self.settings and value.lstrip("-").isdigit():
                self.settings[key] = int(value)

    def _status(self) -> str:
        s = self.settings
        return f"S,{self._fps:.1f},{s['q']},{s['fs']},{s['fec']},{s['pace']},{s['cam']},{s['rate']},0"

    def _control_loop(self) -> None:
        was_active = False
        while self._running:
            try:
                data, (ip, port) = self._sock.recvfrom(128)
            except socket.timeout:
                data = None
            except OSError:
                return
            if data:
                text = data.decode(errors="replace").strip()
                if text.startswith("H,"):
                    self.hellos += 1
                    fields = text[2:].split(",")
                    self._client = (ip, port, int(fields[0]))
                    self._last_rx = time.monotonic()
                    self._apply_settings(fields[1:])
                    self._reply(f"R,1,POWERON,{self._uptime_ms()}")
                    self._reply(f"U,0,BOOTLOADER,{self._uptime_ms() + 5000}")
                    self._reply(self._status())
                elif self._client and ip == self._client[0]:
                    self._last_rx = time.monotonic()
                    if text.startswith("P,"):
                        self.pings += 1
                        self._reply(text)
                    else:
                        self.commands.append(text)
            if was_active and not self._active():
                self.stopped_by_timeout += 1
            was_active = self._active()

    def _video_loop(self) -> None:
        frame_id = 0
        counter = 0
        while self._running:
            time.sleep(1.0 / self._fps)
            if not self._active():
                continue
            s = dict(self.settings)
            jpeg = make_frame(counter, self.commands[-1] if self.commands else "0,0",
                              SIZES.get(s["fs"], SIZES[2]), s["q"])
            counter += 1
            count = (len(jpeg) + CHUNK_BYTES - 1) // CHUNK_BYTES
            ts = self._uptime_ms() & 0xFFFFFFFF
            parity = bytearray(CHUNK_BYTES)
            for index in range(count):
                chunk = jpeg[index * CHUNK_BYTES:(index + 1) * CHUNK_BYTES]
                for k, byte in enumerate(chunk):
                    parity[k] ^= byte
                if self._random.random() >= self._loss:  # perda simulada
                    self._send(struct.pack(">HBBI", frame_id & 0xFFFF, index, count, ts) + chunk)
                if s["pace"]:
                    time.sleep(s["pace"] / 1e6)
            if s["fec"] and self._random.random() >= self._loss:
                payload = struct.pack(">I", len(jpeg)) + bytes(parity)
                self._send(struct.pack(">HBBI", frame_id & 0xFFFF, count, count, ts) + payload)
            frame_id += 1
            self.frames_sent += 1

    def _send(self, packet: bytes) -> None:
        try:
            self._video.sendto(packet, (self._client[0], self._client[2]))
        except OSError:
            pass


def main() -> None:
    parser = argparse.ArgumentParser(description="Simulador do ESP32 do Pantero")
    parser.add_argument("--port", type=int, default=config.CONTROL_PORT)
    parser.add_argument("--bind", default="127.0.0.1", help="use 0.0.0.0 para receber de outra máquina")
    parser.add_argument("--fps", type=int, default=15)
    parser.add_argument("--loss", type=float, default=0.0, help="fração de pacotes de vídeo descartados (0 a 1)")
    args = parser.parse_args()

    robot = FakeRobot(args.port, args.bind, args.fps, args.loss)
    robot.start()
    print(f"robô simulado em {args.bind}:{robot.control_port} (fps={args.fps}, perda={args.loss:.0%}). Ctrl+C para sair.")
    try:
        while True:
            time.sleep(2)
            print(f"  olás: {robot.hellos} | pings: {robot.pings} | comandos: {len(robot.commands)} "
                  f"(último: {robot.commands[-1] if robot.commands else '-'}) | quadros enviados: {robot.frames_sent} "
                  f"| ajustes: {robot.settings}")
    except KeyboardInterrupt:
        robot.stop()


if __name__ == "__main__":
    main()
