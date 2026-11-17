"""Janela principal: vídeo, métricas, ajustes, controles (WASD e joystick), avisos de reset e logs."""
import queue
import time
import tkinter as tk
from typing import Optional, Tuple

from PIL import Image, ImageTk

from . import config
from .controller import AdaptiveController
from .link import RobotLink
from .log import log
from .resets import ResetTracker
from .settings import CAMERA_MODES, FRAME_SIZES, WIFI_RATES, Settings
from .video import VideoReceiver
from .widgets import Joystick, WasdKeys

BG = "#f4f4f2"
PANEL = "#fcfcfb"
TEXT = "#0b0b0b"
MUTED = "#52514e"
GOOD = ("#d1fae5", "#065f46")
BAD = ("#fee2e2", "#991b1b")
LOG_COLORS = {"info": "#d1d5db", "warn": "#fbbf24", "error": "#f87171"}

# tecla -> (x, y). Setas funcionam como WASD.
KEY_VECTORS = {
    "w": (0, 1), "s": (0, -1), "a": (-1, 0), "d": (1, 0),
    "up": (0, 1), "down": (0, -1), "left": (-1, 0), "right": (1, 0),
}
AUTOREPEAT_GRACE_MS = 40  # no Linux, segurar uma tecla gera "soltou + apertou" em sequência


def _clamp(v: float) -> float:
    return max(-1.0, min(1.0, v))


def _fmt(value: Optional[float], unit: str = "", digits: int = 0) -> str:
    return "-" if value is None else f"{value:.{digits}f}{unit}"


class App:
    def __init__(self, root: tk.Tk, robot_ip: str, control_port: int, video_port: int):
        self.root = root
        self.pressed = set()
        self._release_jobs = {}
        self._messages: "queue.Queue[str]" = queue.Queue()
        self._tracker = ResetTracker()
        self._alerts = {}
        self._last_video_seq = 0
        self._last_log_seq = 0
        self._last_controller_at = 0.0
        self._photo: Optional[ImageTk.PhotoImage] = None
        self.settings = Settings()
        self.controller = AdaptiveController(self.settings)

        self._build_ui()
        self.video = VideoReceiver(video_port)
        self.link = RobotLink(self.read_command, self._messages.put, self.settings, robot_ip, control_port,
                              self.video.port)

        root.bind("<KeyPress>", self._on_key_press)
        root.bind("<KeyRelease>", self._on_key_release)
        root.bind("<FocusOut>", lambda _e: self._release_all())
        root.protocol("WM_DELETE_WINDOW", self.close)

        self.video.start()
        self.link.start()
        self._tick()

    # --------------------------------------------------------------- comando
    def read_command(self) -> Tuple[int, int]:
        """Comando atual (x, y) de -100 a 100. O teclado tem prioridade sobre o joystick.
        Chamado pela thread de envio."""
        keys = tuple(self.pressed)
        x = sum(KEY_VECTORS[k][0] for k in keys)
        y = sum(KEY_VECTORS[k][1] for k in keys)
        if x == 0 and y == 0:
            x, y = self.joystick.value
        return round(_clamp(x) * 100), round(_clamp(y) * 100)

    def _on_key_press(self, event) -> None:
        key = event.keysym.lower()
        if key not in KEY_VECTORS:
            return
        job = self._release_jobs.pop(key, None)
        if job:  # era só o auto-repeat do sistema: cancela a soltura pendente
            self.root.after_cancel(job)
        if key not in self.pressed:
            log.info("KEY", f"{key.upper()} pressionada")
            self.pressed.add(key)

    def _on_key_release(self, event) -> None:
        key = event.keysym.lower()
        if key in KEY_VECTORS:
            self._release_jobs[key] = self.root.after(AUTOREPEAT_GRACE_MS, lambda: self._release(key))

    def _release(self, key: str) -> None:
        self._release_jobs.pop(key, None)
        if key in self.pressed:
            self.pressed.discard(key)
            log.info("KEY", f"{key.upper()} solta")

    def _release_all(self) -> None:
        if self.pressed:
            log.warn("KEY", "janela perdeu o foco, soltando todas as teclas")
        self.pressed.clear()

    # --------------------------------------------------------------- interface
    def _build_ui(self) -> None:
        root = self.root
        root.title("Pantero")
        root.configure(bg=BG)

        header = tk.Frame(root, bg=BG)
        header.pack(fill="x", padx=12, pady=(10, 4))
        tk.Label(header, text="Pantero", font=("TkDefaultFont", 16, "bold"), bg=BG, fg=TEXT).pack(side="left")
        self.status = tk.Label(header, text="Desconectado", padx=12, pady=3, font=("TkDefaultFont", 10, "bold"))
        self.status.pack(side="right")

        self.alerts_frame = tk.Frame(root, bg=BG)
        self.alerts_frame.pack(fill="x", padx=12)

        body = tk.Frame(root, bg=BG)
        body.pack(padx=12, pady=6)
        width, height = 320 * config.VIDEO_SCALE, 240 * config.VIDEO_SCALE
        self.canvas = tk.Canvas(body, width=width, height=height, bg="#111", highlightthickness=0)
        self.canvas.grid(row=0, column=0, padx=(0, 12))
        self._video_item = self.canvas.create_image(width // 2, height // 2)
        self._placeholder = self.canvas.create_text(width // 2, height // 2, text="Aguardando vídeo…", fill="#ddd",
                                                    font=("TkDefaultFont", 14))
        self.metrics_label = tk.Label(body, text="", bg=BG, fg=MUTED, justify="left", anchor="w",
                                      font=("TkFixedFont", 9))
        self.metrics_label.grid(row=1, column=0, sticky="w", pady=(6, 0))

        side = tk.Frame(body, bg=BG)
        side.grid(row=0, column=1, rowspan=2, sticky="n")
        self._build_controls(side)
        self._build_settings(side)

        self._show_logs = tk.BooleanVar(value=True)
        tk.Checkbutton(root, text="Mostrar logs", variable=self._show_logs, command=self._toggle_logs, bg=BG,
                       fg=MUTED, activebackground=BG).pack(anchor="w", padx=12)
        self.log_text = tk.Text(root, height=8, bg="#111", fg="#d1d5db", font=("TkFixedFont", 9), wrap="word",
                                state="disabled", relief="flat")
        for level, color in LOG_COLORS.items():
            self.log_text.tag_configure(level, foreground=color)
        self.log_text.pack(fill="x", padx=12, pady=(0, 12))

    def _build_controls(self, parent) -> None:
        controls = tk.Frame(parent, bg=PANEL, padx=12, pady=10, highlightbackground="#e4e3df", highlightthickness=1)
        controls.pack(fill="x")
        self.wasd = WasdKeys(controls)
        self.wasd.pack(pady=(0, 8))
        self.joystick = Joystick(controls)
        self.joystick.pack()
        self.command_label = tk.Label(controls, text="x=+0  y=+0", bg=PANEL, fg=MUTED, font=("TkFixedFont", 10))
        self.command_label.pack(pady=(8, 0))

    def _build_settings(self, parent) -> None:
        """Ajustes do vídeo e do rádio. Cada mudança vai ao ESP32 no próximo olá (até 1 s)."""
        box = tk.LabelFrame(parent, text=" Vídeo e rádio ", bg=PANEL, fg=TEXT, padx=10, pady=6)
        box.pack(fill="x", pady=(10, 0))
        s = self.settings

        def bind(var, attr, cast):
            var.trace_add("write", lambda *_: setattr(s, attr, cast(var.get())))
            return var

        self.var_adaptive = bind(tk.BooleanVar(value=s.adaptive), "adaptive", bool)
        tk.Checkbutton(box, text="Adaptativo (o PC ajusta qualidade e resolução)", variable=self.var_adaptive,
                       bg=PANEL, anchor="w").pack(fill="x")

        self.var_quality = bind(tk.IntVar(value=s.quality), "quality", int)
        tk.Scale(box, from_=10, to=50, orient="horizontal", variable=self.var_quality, bg=PANEL, highlightthickness=0,
                 label="Qualidade JPEG (10 = melhor)").pack(fill="x")

        self.var_fs = bind(tk.IntVar(value=s.frame_size), "frame_size", int)
        row = tk.Frame(box, bg=PANEL)
        row.pack(fill="x")
        tk.Label(row, text="Imagem:", bg=PANEL).pack(side="left")
        for index, (w, h) in FRAME_SIZES.items():
            tk.Radiobutton(row, text=f"{w}x{h}", value=index, variable=self.var_fs, bg=PANEL).pack(side="left")

        self.var_fec = bind(tk.BooleanVar(value=s.fec), "fec", bool)
        tk.Checkbutton(box, text="FEC (pacote de paridade: recupera 1 perda)", variable=self.var_fec, bg=PANEL,
                       anchor="w").pack(fill="x")

        self.var_pace = bind(tk.IntVar(value=s.pace_us), "pace_us", int)
        tk.Scale(box, from_=0, to=5000, resolution=250, orient="horizontal", variable=self.var_pace, bg=PANEL,
                 highlightthickness=0, label="Pacing (µs entre pacotes)").pack(fill="x")

        self.var_cam = bind(tk.IntVar(value=s.cam_mode), "cam_mode", int)
        tk.Label(box, text="Câmera:", bg=PANEL, anchor="w").pack(fill="x")
        for index, text in CAMERA_MODES.items():
            tk.Radiobutton(box, text=text, value=index, variable=self.var_cam, bg=PANEL, anchor="w").pack(fill="x")

        self.var_rate = bind(tk.IntVar(value=s.wifi_rate), "wifi_rate", int)
        tk.Label(box, text="Taxa do rádio:", bg=PANEL, anchor="w").pack(fill="x")
        row = tk.Frame(box, bg=PANEL)
        row.pack(fill="x")
        for index, text in WIFI_RATES.items():
            tk.Radiobutton(row, text=text, value=index, variable=self.var_rate, bg=PANEL).pack(side="left")

    def _toggle_logs(self) -> None:
        if self._show_logs.get():
            self.log_text.pack(fill="x", padx=12, pady=(0, 12))
        else:
            self.log_text.pack_forget()

    # --------------------------------------------------------------- atualização periódica
    def _tick(self) -> None:
        self._update_video()
        self._update_messages()
        self._update_logs()
        self._update_status()
        self._run_controller()
        self.root.after(20, self._tick)

    def _update_video(self) -> None:
        seq, image = self.video.latest()
        if image is not None and seq != self._last_video_seq:
            self._last_video_seq = seq
            scaled = image.resize((320 * config.VIDEO_SCALE, 240 * config.VIDEO_SCALE), Image.BILINEAR)
            self._photo = ImageTk.PhotoImage(scaled)  # guarda a referência, senão o Tk apaga a imagem
            self.canvas.itemconfig(self._video_item, image=self._photo)
        showing = image is not None and not self.video.stale
        self.canvas.itemconfig(self._video_item, state="normal" if showing else "hidden")
        self.canvas.itemconfig(self._placeholder, state="hidden" if showing else "normal",
                               text="Sem imagem da câmera" if image is not None else "Aguardando vídeo…")

    def _update_messages(self) -> None:
        """Avisos de reset recebidos do ESP32 (a thread de rede só enfileira)."""
        while True:
            try:
                text = self._messages.get_nowait()
            except queue.Empty:
                return
            update = self._tracker.update(text, time.monotonic() * 1000)
            if update is None:
                log.warn("RX", f"mensagem desconhecida: {text}")
                continue
            info = update.info
            if update.changed:
                summary = f"{info.label} ligado ha {info.uptime_ms / 1000:.1f} s; ultimo reset: {info.name} ({info.code})"
                if update.rebooted:
                    log.error(info.label, f"REINICIOU! {summary}")
                elif info.abnormal:
                    log.warn(info.label, summary)
                else:
                    log.info(info.label, summary)
            if update.changed or update.alert != (self._alerts.get(info.board) is not None):
                self._alerts[info.board] = info if update.alert else None
                self._render_alerts()

    def _render_alerts(self) -> None:
        for child in self.alerts_frame.winfo_children():
            child.destroy()
        for board, info in self._alerts.items():
            if info is None:
                continue
            row = tk.Frame(self.alerts_frame, bg=BAD[0])
            row.pack(fill="x", pady=2)
            extra = " (alimentação fraca)" if info.name == "BROWNOUT" else ""
            tk.Label(row, text=f"{info.label} reiniciou: {info.name}{extra} — ligado há {info.uptime_ms // 1000} s",
                     bg=BAD[0], fg=BAD[1]).pack(side="left", padx=8, pady=6)
            tk.Button(row, text="Ok", command=lambda b=board: self._dismiss(b)).pack(side="right", padx=8)

    def _dismiss(self, board: str) -> None:
        self._alerts[board] = None
        self._render_alerts()

    def _update_logs(self) -> None:
        lines = log.since(self._last_log_seq)
        if not lines:
            return
        self._last_log_seq = lines[-1].seq
        self.log_text.configure(state="normal")
        for line in lines:
            self.log_text.insert("end", f"{line.time} [{line.tag}] {line.text}\n", line.level)
        total = int(self.log_text.index("end-1c").split(".")[0])
        if total > 300:  # mantém o painel leve
            self.log_text.delete("1.0", f"{total - 300}.0")
        self.log_text.configure(state="disabled")
        self.log_text.see("end")

    def _update_status(self) -> None:
        connected = self.link.connected
        bg, fg = GOOD if connected else BAD
        self.status.configure(text="Conectado" if connected else "Desconectado", bg=bg, fg=fg)

        x, y = self.read_command()
        self.command_label.configure(text=f"x={x:+d}  y={y:+d}")
        self.wasd.show(x / 100, y / 100)

        stats = self.video.stats()
        rtt_avg, rtt_p95 = self.link.rtt_stats()
        esp = self.link.esp_status
        self.metrics_label.configure(text=(
            f"vídeo   {stats.fps:4.1f} fps | {stats.bitrate_mbps:4.2f} Mbps | {stats.frame_kb:4.1f} KB/quadro\n"
            f"atraso  p50 {_fmt(stats.latency_p50_ms, ' ms')} | p95 {_fmt(stats.latency_p95_ms, ' ms')} | "
            f"jitter p95 {_fmt(stats.jitter_p95_ms, ' ms')}\n"
            f"perda   {stats.loss_pct:4.1f}% dos quadros | recuperados por FEC: {stats.recovered} | descartados: {stats.dropped}\n"
            f"controle (ping)  média {_fmt(rtt_avg, ' ms')} | p95 {_fmt(rtt_p95, ' ms')}\n"
            f"ESP32   {esp.get('fps', 0):.1f} fps enviados | pacotes que não saíram: {int(esp.get('lost', 0))}\n"
            f"ajustes {self.settings.describe()}"))

    def _run_controller(self) -> None:
        """Compressão adaptativa: uma decisão por segundo, só se estiver ligada."""
        now = time.monotonic()
        if not self.settings.adaptive or now - self._last_controller_at < 1.0:
            return
        self._last_controller_at = now
        stats = self.video.stats()
        change = self.controller.update(stats.loss_pct, stats.latency_p95_ms, stats.fps,
                                        self.link.esp_status.get("fps", 0.0), now)
        if change:
            log.info("ADAPT", f"{change} -> {self.settings.describe()}")
            self.var_quality.set(self.settings.quality)  # a barra acompanha o que o controlador decidiu
            self.var_fs.set(self.settings.frame_size)

    # --------------------------------------------------------------- encerramento
    def close(self) -> None:
        self.link.stop()   # manda a parada ao robô
        self.video.stop()
        self.root.destroy()
