#!/usr/bin/env python3
"""Mede o vídeo contra o robô real, cenário por cenário, com a mesma régua.

Conecte o PC ao Wi-Fi "Pantero" e rode, por exemplo:
  python3 pc/measure.py                       # todos os cenários, 3 rodadas intercaladas
  python3 pc/measure.py base nopace fec        # só alguns cenários
  python3 pc/measure.py --rounds 5 --duration 10

Cada cenário aplica um conjunto de ajustes (qualidade, resolução, pacing, FEC, modo da câmera, taxa do rádio,
compressão adaptativa), espera estabilizar e mede fps, bitrate, atraso relativo, jitter, perda e o ping do
controle. As rodadas são intercaladas em ordem embaralhada, e o resultado é a mediana, porque a rede varia.
O robô fica parado (o comando é sempre 0,0).
"""
import argparse
import random
import statistics as st
import time

from pantero import config
from pantero.controller import AdaptiveController
from pantero.link import RobotLink
from pantero.log import log
from pantero.settings import Settings
from pantero.video import VideoReceiver

SCENARIOS = {
    "base": dict(),                      # ajustes padrão (inclui pacing de 4000 µs)
    "nopace": dict(pace_us=0),           # sem pacing: pacotes de cada quadro em rajada
    "pace1500": dict(pace_us=1500),
    "pace2500": dict(pace_us=2500),
    "q10": dict(quality=10),
    "q25": dict(quality=25),
    "q35": dict(quality=35),
    "160x120": dict(frame_size=0),
    "240x176": dict(frame_size=1),
    "fec": dict(fec=True),
    "cam2": dict(cam_mode=1),
    "rate54": dict(wifi_rate=1),
    "mcs7": dict(wifi_rate=2),
    "adaptive": dict(adaptive=True),
}
COLUMNS = [("fps", "fps"), ("mbps", "Mbps"), ("kb", "KB/q"), ("p50", "atr p50"), ("p95", "atr p95"),
           ("jit", "jit p95"), ("loss", "perda%"), ("rec", "FEC rec"), ("rtt", "rtt ms"), ("rtt95", "rtt p95")]


def run_scenario(name: str, overrides: dict, ip: str, port: int, warmup: float, duration: float) -> dict:
    settings = Settings(**overrides)
    video = VideoReceiver(port=0)
    video.start()
    link = RobotLink(lambda: (0, 0), lambda _m: None, settings, ip, port, video.port)
    link.start()
    controller = AdaptiveController(settings) if settings.adaptive else None

    start = time.monotonic()
    measured = False
    last_control = 0.0
    while time.monotonic() - start < warmup + duration:
        time.sleep(0.2)
        now = time.monotonic()
        if not measured and now - start >= warmup:
            video.reset_metrics()
            measured = True
        if controller and now - last_control >= 1.0:  # compressão adaptativa: uma decisão por segundo
            last_control = now
            stats = video.stats()
            change = controller.update(stats.loss_pct, stats.latency_p95_ms, stats.fps, link.esp_status.get("fps", 0.0), now)
            if change:
                log.info("ADAPT", f"{change} -> {settings.describe()}")

    stats = video.stats()
    rtt_avg, rtt_p95 = link.rtt_stats()
    result = dict(name=name, fps=stats.fps, mbps=stats.bitrate_mbps, kb=stats.frame_kb, p50=stats.latency_p50_ms,
                  p95=stats.latency_p95_ms, jit=stats.jitter_p95_ms, loss=stats.loss_pct, rec=stats.recovered,
                  rtt=rtt_avg, rtt95=rtt_p95, connected=link.connected, final=settings.describe())
    link.stop()
    video.stop()
    return result


def median(values):
    values = [v for v in values if v is not None]
    return st.median(values) if values else None


def fmt(value) -> str:
    return "-" if value is None else f"{value:7.1f}"


def main() -> None:
    parser = argparse.ArgumentParser(description="Mede o vídeo do Pantero contra o robô real")
    parser.add_argument("scenarios", nargs="*", help=f"cenários ({', '.join(SCENARIOS)}); padrão: todos")
    parser.add_argument("--ip", default=config.ROBOT_IP)
    parser.add_argument("--control-port", type=int, default=config.CONTROL_PORT)
    parser.add_argument("--rounds", type=int, default=3, help="repetições intercaladas (padrão: 3)")
    parser.add_argument("--warmup", type=float, default=4.0, help="segundos de estabilização (padrão: 4)")
    parser.add_argument("--duration", type=float, default=8.0, help="segundos medidos (padrão: 8)")
    args = parser.parse_args()

    names = args.scenarios or list(SCENARIOS)
    unknown = [n for n in names if n not in SCENARIOS]
    if unknown:
        parser.error(f"cenário desconhecido: {', '.join(unknown)}")

    rnd = random.Random(7)
    results = {n: [] for n in names}
    for round_number in range(1, args.rounds + 1):
        order = names[:]
        rnd.shuffle(order)
        for name in order:
            r = run_scenario(name, SCENARIOS[name], args.ip, args.control_port, args.warmup, args.duration)
            results[name].append(r)
            print(f"[rodada {round_number}/{args.rounds}] {name:10} fps {fmt(r['fps'])} p95 {fmt(r['p95'])} "
                  f"perda {fmt(r['loss'])} rtt {fmt(r['rtt'])}" + ("" if r["connected"] else "  (SEM CONEXAO)"), flush=True)

    print("\nMediana de %d rodadas (atraso e jitter em ms; atraso relativo ao melhor caso):" % args.rounds)
    print(f"{'cenário':10}" + "".join(f"{label:>9}" for _, label in COLUMNS))
    for name in names:
        rows = results[name]
        print(f"{name:10}" + "".join(f"{fmt(median([r[key] for r in rows])):>9}" for key, _ in COLUMNS))


if __name__ == "__main__":
    main()
