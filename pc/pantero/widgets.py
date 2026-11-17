"""Componentes da interface: joystick na tela e indicador das teclas WASD."""
import math
import tkinter as tk
from typing import Tuple

BASE = "#e4e3df"
BORDER = "#9ca3af"
ACCENT = "#2a78d6"
TEXT = "#0b0b0b"
PANEL = "#fcfcfb"


class Joystick(tk.Canvas):
    """Joystick analógico para mouse. `value` é (x, y) de -1 a 1 (y positivo = frente)."""

    SIZE = 200
    KNOB = 70

    def __init__(self, master, **kwargs):
        super().__init__(master, width=self.SIZE, height=self.SIZE, bg=PANEL, highlightthickness=0, **kwargs)
        self._radius = (self.SIZE - self.KNOB) / 2
        center = self.SIZE / 2
        self._center = center
        self.create_oval(4, 4, self.SIZE - 4, self.SIZE - 4, fill=BASE, outline=BORDER, width=2)
        self._knob = self.create_oval(0, 0, 0, 0, fill=ACCENT, outline="")
        self._value = (0.0, 0.0)
        self._move_knob(0, 0)

        self.bind("<ButtonPress-1>", self._on_drag)
        self.bind("<B1-Motion>", self._on_drag)
        self.bind("<ButtonRelease-1>", self._on_release)

    @property
    def value(self) -> Tuple[float, float]:
        return self._value

    def _move_knob(self, dx: float, dy: float) -> None:
        half = self.KNOB / 2
        cx, cy = self._center + dx, self._center + dy
        self.coords(self._knob, cx - half, cy - half, cx + half, cy + half)

    def _on_drag(self, event) -> None:
        dx, dy = event.x - self._center, event.y - self._center
        dist = math.hypot(dx, dy)
        if dist > self._radius:  # limita ao círculo
            dx, dy = dx / dist * self._radius, dy / dist * self._radius
        self._move_knob(dx, dy)
        self._value = (dx / self._radius, -dy / self._radius)

    def _on_release(self, _event) -> None:
        self._move_knob(0, 0)
        self._value = (0.0, 0.0)


class WasdKeys(tk.Frame):
    """Teclas W, A, S, D desenhadas na tela; acendem conforme o comando atual."""

    THRESHOLD = 0.3

    def __init__(self, master, **kwargs):
        super().__init__(master, bg=PANEL, **kwargs)
        self._keys = {}
        layout = {"w": (0, 1), "a": (1, 0), "s": (1, 1), "d": (1, 2)}
        for key, (row, col) in layout.items():
            label = tk.Label(self, text=key.upper(), width=4, height=2, font=("TkDefaultFont", 12, "bold"),
                             bg=PANEL, fg=TEXT, relief="solid", borderwidth=2)
            label.grid(row=row, column=col, padx=3, pady=3)
            self._keys[key] = label

    def show(self, x: float, y: float) -> None:
        active = {"w": y > self.THRESHOLD, "s": y < -self.THRESHOLD, "a": x < -self.THRESHOLD, "d": x > self.THRESHOLD}
        for key, label in self._keys.items():
            on = active[key]
            label.configure(bg=ACCENT if on else PANEL, fg="white" if on else TEXT)
