#!/usr/bin/env python3
"""Programa do PC do robô Pantero: vídeo e controle por UDP.

Conecte o PC ao Wi-Fi "Pantero" (senha 12345678) e rode:  python3 pc/main.py
Controle: teclas W A S D (ou setas) e o joystick na tela, com o mouse.
"""
import argparse
import tkinter as tk

from pantero import config
from pantero.gui import App


def main() -> None:
    parser = argparse.ArgumentParser(description="Controle e vídeo do robô Pantero por UDP")
    parser.add_argument("--ip", default=config.ROBOT_IP, help="endereço do ESP32 (padrão: %(default)s)")
    parser.add_argument("--control-port", type=int, default=config.CONTROL_PORT, help="porta UDP de controle do ESP32")
    parser.add_argument("--video-port", type=int, default=config.VIDEO_PORT, help="porta UDP local que recebe o vídeo")
    args = parser.parse_args()

    root = tk.Tk()
    App(root, args.ip, args.control_port, args.video_port)
    root.mainloop()


if __name__ == "__main__":
    main()
