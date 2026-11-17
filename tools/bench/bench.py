#!/usr/bin/env python3
"""Banco de testes de vídeo/Wi-Fi do Pantero (use com o firmware do ambiente esp32test).

  python3 tools/bench/bench.py run  <saida.json>   # roda a matriz de testes
  python3 tools/bench/bench.py show <saida.json>   # imprime as tabelas

Métricas por teste (janela medida: depois de WARM_S segundos de aquecimento):
  fps, kbps, bytes/frame     vazão efetiva do vídeo
  gap p50/p95/p99/max (ms)   intervalo entre frames (jitter); "travada" = gap > 200 ms
  lat p50/p95/max (ms)       latência RELATIVA: (chegada no PC - timestamp de captura no ESP32) menos o
                             melhor caso da própria corrida. Mede fila/atraso acumulado, não a latência
                             absoluta (os relógios não estão sincronizados).
  ping avg/p95/max, perda    RTT ICMP durante o vídeo = latência do canal de CONTROLE sob carga
  rx_mbps, rssi              taxa PHY do enlace ESP32 -> PC e sinal
"""
import asyncio, json, re, socket, statistics as st, struct, subprocess, sys, threading, time, urllib.request

HOST = "192.168.4.1"
IFACE = "wlp0s20f3"
DUR_S = 12
WARM_S = 2

BASE = dict(q=15, fs="qvga", mode=0, nodelay=1, maxfps=0, ondemand=0, bw=20, proto="bgn", tx=78)
CAM_KEYS = ("fb", "grab", "xclk", "fbloc")
CAM_DEFAULT = dict(fb=2, grab=1, xclk=20000000, fbloc=1)


def http(path, port=83, timeout=5):
    return urllib.request.urlopen(f"http://{HOST}:{port}{path}", timeout=timeout).read().decode()


def cfg(**kw):
    return json.loads(http("/cfg?" + "&".join(f"{k}={v}" for k, v in kw.items())))


def stats():
    return json.loads(http("/stats"))


def ping_ok():
    return subprocess.run(["ping", "-c", "1", "-W", "1", HOST], capture_output=True).returncode == 0


def ensure_link(max_s=40):
    t0 = time.time()
    while time.time() - t0 < max_s:
        if ping_ok():
            return True
        subprocess.run(["nmcli", "con", "up", "Pantero"], capture_output=True)
        time.sleep(1.5)
    return False


def link_info():
    out = subprocess.run(["iw", "dev", IFACE, "link"], capture_output=True, text=True).stdout
    g = lambda pat: (re.search(pat, out) or [None, None])[1]
    rx = g(r"rx bitrate:\s*([\d.]+)")
    sig = g(r"signal:\s*(-?\d+)")
    return dict(rx_mbps=float(rx) if rx else None, rssi=int(sig) if sig else None)


def pct(v, p):
    v = sorted(v)
    return v[min(len(v) - 1, int(len(v) * p / 100))] if v else None


# ------------------------------- cliente MJPEG -------------------------------
class Mjpeg:
    def __init__(self, port=81):
        self.s = socket.create_connection((HOST, port), timeout=4)
        self.s.settimeout(4)
        self.s.sendall(b"GET /stream HTTP/1.1\r\nHost: x\r\n\r\n")
        self.rb = bytearray()
        self.rem = 0
        hdr = bytearray()
        while b"\r\n\r\n" not in hdr:
            d = self.s.recv(4096)
            if not d:
                raise EOFError("sem resposta")
            hdr += d
        i = hdr.index(b"\r\n\r\n")
        self.chunked = b"chunked" in bytes(hdr[:i]).lower()
        self.mp = bytearray(self._decode(bytes(hdr[i + 4:])))

    def _decode(self, data):
        if not self.chunked:
            return data
        self.rb += data
        out = bytearray()
        while True:
            if self.rem == 0:
                i = self.rb.find(b"\r\n")
                if i < 0:
                    break
                if i == 0:
                    del self.rb[:2]
                    continue
                size = int(bytes(self.rb[:i]), 16)
                del self.rb[: i + 2]
                if size == 0:
                    raise EOFError("fim do stream")
                self.rem = size
            take = min(self.rem, len(self.rb))
            if take == 0:
                break
            out += self.rb[:take]
            del self.rb[:take]
            self.rem -= take
        return out

    def frames(self, deadline):
        """Gera (t_chegada, bytes, seq, ts_esp) até o deadline."""
        while time.perf_counter() < deadline:
            while True:
                b = self.mp.find(b"--frame\r\n")
                if b < 0:
                    break
                h = self.mp.find(b"\r\n\r\n", b)
                if h < 0:
                    break
                head = bytes(self.mp[b:h]).decode("latin1")
                clen = int(re.search(r"Content-Length:\s*(\d+)", head).group(1))
                if len(self.mp) < h + 4 + clen:
                    break
                seq = re.search(r"X-Seq:\s*(\d+)", head)
                ts = re.search(r"X-Ts:\s*(\d+)", head)
                del self.mp[: h + 4 + clen]
                yield (time.perf_counter(), clen, int(seq.group(1)) if seq else 0, int(ts.group(1)) if ts else 0)
            try:
                d = self.s.recv(65536)
            except socket.timeout:
                continue
            if not d:
                return
            self.mp += self._decode(d)

    def close(self):
        self.s.close()


async def ws_frames(deadline):
    import websockets
    out = []
    async with websockets.connect(f"ws://{HOST}/wsv", max_size=2 ** 22, ping_interval=None) as w:
        while time.perf_counter() < deadline:
            try:
                m = await asyncio.wait_for(w.recv(), 3)
            except asyncio.TimeoutError:
                continue
            seq, ts = struct.unpack("<II", m[:8])
            out.append((time.perf_counter(), len(m) - 8, seq, ts))
    return out


# -------------------------------- métricas --------------------------------
def analyze(frames, t_start):
    f = [x for x in frames if x[0] >= t_start + WARM_S]
    if len(f) < 4:
        return dict(frames=len(f), fps=0.0)
    ts = [x[0] for x in f]
    win = ts[-1] - ts[0]
    gaps = [(b - a) * 1000 for a, b in zip(ts, ts[1:])]
    lat = [(x[0] - ts[0]) * 1000 - (x[3] - f[0][3]) for x in f]
    base = min(lat)
    lat = [v - base for v in lat]
    seqs = [x[2] for x in f]
    skipped = (seqs[-1] - seqs[0] + 1) - len(f) if seqs[-1] else None
    return dict(
        frames=len(f), fps=round((len(f) - 1) / win, 1),
        kbps=round(sum(x[1] for x in f[1:]) * 8 / win / 1000), bpf=round(st.mean(x[1] for x in f)),
        gap50=round(pct(gaps, 50)), gap95=round(pct(gaps, 95)), gap99=round(pct(gaps, 99)), gapmax=round(max(gaps)),
        stalls=sum(1 for g in gaps if g > 200), lat50=round(pct(lat, 50)), lat95=round(pct(lat, 95)), latmax=round(max(lat)),
        skipped=skipped,
    )


def ping_summary(lines, skip):
    v = [float(m.group(1)) for m in (re.search(r"time=([\d.]+)", l) for l in lines) if m]
    v = v[int(skip / 0.2):]
    return dict(ping_avg=round(st.mean(v), 1) if v else None, ping_p95=round(pct(v, 95), 1) if v else None,
                ping_max=round(max(v), 1) if v else None)


# ---------------------------------- teste ----------------------------------
def run_test(name, setup, transport="mjpeg", dur=DUR_S):
    conf = dict(BASE)
    conf.update({k: v for k, v in setup.items() if k not in CAM_KEYS})
    cam = {k: setup[k] for k in CAM_KEYS if k in setup}
    camoff = setup.get("camoff")
    conf.pop("camoff", None)
    camprio = conf.pop("camprio", None)
    clkrc = conf.pop("clkrc", None)

    def apply():
        cfg(**conf)
        if cam:
            cfg(reinit=1, **cam)
        if camoff:
            cfg(cam="off")
        if camprio:                      # depois do reinit: a cam_task é recriada com prioridade 23
            cfg(camprio=camprio)
        if clkrc is not None:            # depois do reinit: o init do sensor reescreve o registrador
            cfg(clkrc=clkrc)

    ensure_link()
    try:
        apply()
    except Exception:
        ensure_link()
        apply()
    time.sleep(2.5)
    if not ensure_link():
        return dict(name=name, error="sem enlace")

    n = int((dur + 1) / 0.2)
    pp = subprocess.Popen(["ping", "-i", "0.2", "-W", "1", "-c", str(n), HOST], stdout=subprocess.PIPE, text=True)
    t0 = time.perf_counter()
    deadline = t0 + dur
    info = {}

    def sample():
        time.sleep(dur / 2)
        info.update(link_info())

    th = threading.Thread(target=sample)
    th.start()
    try:
        if transport == "none":      # só ping: mede o enlace/ESP32 ocioso
            time.sleep(dur)
            frames = []
        elif transport == "ws":
            frames = asyncio.run(ws_frames(deadline))
        else:
            m = Mjpeg()
            frames = list(m.frames(deadline))
            m.close()
    except Exception as e:
        frames = []
        info["error"] = str(e)
    th.join()
    pout = pp.communicate()[0].splitlines()
    sent_replies = [l for l in pout if "time=" in l]
    res = dict(name=name, setup=setup, transport=transport)
    res.update(analyze(frames, t0))
    res.update(ping_summary(pout, WARM_S))
    res["ping_loss"] = round(100 * (1 - len(sent_replies) / max(1, n)), 1)
    res.update(info)
    try:
        s = stats()
        res.update(heap=s["heap"], minheap=s["minheap"], ws_drop=s["wsDropped"], rssi_esp=s["rssi"])
    except Exception:
        pass
    return res


def matrix():
    T = []

    def add(name, **kw):
        T.append((name, kw, kw.pop("_t", "mjpeg")))

    # Z) ESP32 ocioso (sem vídeo): o custo da câmera sobre o Wi-Fi/controle
    add("Z1 ocioso, camera ligada (padrao)", _t="none")
    add("Z2 ocioso, camera DESLIGADA", camoff=True, _t="none")
    add("Z3 ocioso, xclk 10 MHz", fb=2, grab=1, xclk=10000000, _t="none")
    add("Z4 ocioso, grab=WHEN_EMPTY", fb=2, grab=0, xclk=20000000, _t="none")
    add("Z5 ocioso, fb=1 em PSRAM", fb=1, grab=1, xclk=20000000, fbloc=1, _t="none")
    add("Z6 ocioso, fb=1 em DRAM", fb=1, grab=1, xclk=20000000, fbloc=0, _t="none")
    add("Z7 ocioso, fb=1 DRAM + WHEN_EMPTY", fb=1, grab=0, xclk=20000000, fbloc=0, _t="none")
    for i in (1, 2, 3):                                                      # A) ruído do teste
        add(f"A{i} baseline com video (chunked, nodelay, QVGA q15)")
    add("B1 envio raw (1 send por frame)", mode=1)                            # B) algoritmo de envio
    add("B2 chunked sem TCP_NODELAY (Nagle)", nodelay=0)
    add("B3 raw sem TCP_NODELAY", mode=1, nodelay=0)
    for q in (10, 25, 40):                                                   # C) compressão JPEG
        add(f"C QVGA q{q}", q=q)
    add("D1 QQVGA 160x120 q15", fs="qqvga")                                   # D) resolução
    add("D2 HQVGA 240x176 q15", fs="hqvga")
    add("D3 CIF 352x288 q15", fs="cif")
    add("D4 VGA 640x480 q15", fs="vga")
    for f in (10, 15, 20):                                                   # E) cadência / captura
        add(f"E limite {f} fps (app)", maxfps=f)
    add("E4 captura sob demanda", ondemand=1)
    add("F1 canal 40 MHz (HT40)", bw=40)                                      # F) Wi-Fi
    add("F2 so g/n (sem 11b)", proto="gn")
    add("F3 so n", proto="n")
    for txv, lbl in ((34, "8.5"), (60, "15"), (84, "21 (maximo)")):
        add(f"F tx {lbl} dBm", tx=txv)
    add("G1 video, xclk 10 MHz", fb=2, grab=1, xclk=10000000)                 # G) câmera com vídeo
    add("G2 video, grab=WHEN_EMPTY", fb=2, grab=0, xclk=20000000)
    add("G3 video, fb=1 em DRAM", fb=1, grab=1, xclk=20000000, fbloc=0)
    add("G4 video, fb=1 DRAM + WHEN_EMPTY", fb=1, grab=0, xclk=20000000, fbloc=0)
    add("G5 video, fb=3 PSRAM", fb=3, grab=1, xclk=20000000, fbloc=1)
    add("H1 WebSocket binario (descarta se lento)", _t="ws")                   # H) transporte
    add("H2 WebSocket binario QQVGA", fs="qqvga", _t="ws")
    return T


def cmd_run(out):
    T = matrix()
    res = []
    ensure_link()
    cfg(**BASE)
    cfg(reinit=1, **CAM_DEFAULT)
    for i, (name, kw, tr) in enumerate(T, 1):
        print(f"[{i}/{len(T)}] {name}", flush=True)
        r = run_test(name, kw, tr)
        print("   ", {k: r.get(k) for k in ("fps", "kbps", "gap95", "stalls", "lat95", "ping_avg", "ping_p95", "rx_mbps", "error")}, flush=True)
        res.append(r)
        json.dump(res, open(out, "w"), indent=1)
        nxt = T[i][1] if i < len(T) else {}
        cam_keys = CAM_KEYS + ("camoff",)
        if any(k in kw for k in cam_keys) and not any(k in nxt for k in cam_keys):
            cfg(reinit=1, **CAM_DEFAULT)   # não deixa a câmera alterada vazar para o teste seguinte
            time.sleep(1)
    cfg(**BASE)
    cfg(reinit=1, **CAM_DEFAULT)
    print("pronto:", out)


REP_CFGS = [
    # nome, setup, transporte, duração
    ("R0 ocioso, camera ligada", {}, "none", 10),
    ("R1 ocioso, camera DESLIGADA", dict(camoff=True), "none", 10),
    ("V0 video baseline (chunked QVGA q15, bgn)", {}, "mjpeg", 12),
    ("V1 video grab=WHEN_EMPTY", dict(fb=2, grab=0, xclk=20000000, fbloc=1), "mjpeg", 12),
    ("V2 video so g/n (sem 11b)", dict(proto="gn"), "mjpeg", 12),
    ("V3 video WHEN_EMPTY + g/n", dict(fb=2, grab=0, xclk=20000000, fbloc=1, proto="gn"), "mjpeg", 12),
    ("V4 video WHEN_EMPTY + g/n + raw", dict(fb=2, grab=0, xclk=20000000, fbloc=1, proto="gn", mode=1), "mjpeg", 12),
    ("V5 video WHEN_EMPTY + g/n + QQVGA", dict(fb=2, grab=0, xclk=20000000, fbloc=1, proto="gn", fs="qqvga"), "mjpeg", 12),
    ("W1 WebSocket (fila=2)", dict(fb=2, grab=1, xclk=20000000, fbloc=1), "ws", 12),
    ("W2 WebSocket (fila=2) + WHEN_EMPTY + g/n", dict(fb=2, grab=0, xclk=20000000, fbloc=1, proto="gn"), "ws", 12),
]


def cmd_rep(out, blocks=4, cfgs=None):
    import random
    rnd = random.Random(7)
    res = []
    ensure_link()
    cfg(**BASE)
    cfg(reinit=1, **CAM_DEFAULT)
    for b in range(1, blocks + 1):
        order = (cfgs or REP_CFGS)[:]
        rnd.shuffle(order)
        for name, kw, tr, dur in order:
            print(f"[bloco {b}/{blocks}] {name}", flush=True)
            r = run_test(name, dict(kw), tr, dur)
            r["block"] = b
            print("   ", {k: r.get(k) for k in ("fps", "gap95", "lat95", "ping_avg", "ping_p95", "error")}, flush=True)
            res.append(r)
            json.dump(res, open(out, "w"), indent=1)
            # sempre volta a câmera ao padrão antes do próximo teste
            cfg(**BASE)
            cfg(reinit=1, **CAM_DEFAULT)
            time.sleep(1)
    print("pronto:", out)


PRIO_CFGS = [(f"P{p} ocioso, cam_task prio {p}", dict(camprio=p), "none", 10) for p in (23, 17, 10, 5, 2)] + [
    (f"PV{p} video, cam_task prio {p}", dict(camprio=p), "mjpeg", 12) for p in (23, 10, 5, 2)]


def cmd_prio(out):
    cmd_rep(out, 4, PRIO_CFGS)


CLK_CFGS = [("K0 ocioso, camera desligada", dict(camoff=True), "none", 10)] + [
    (f"K{c} ocioso, CLKRC={c}", dict(clkrc=c), "none", 10) for c in (0, 1, 3, 7)] + [
    (f"KV{c} video, CLKRC={c}", dict(clkrc=c), "mjpeg", 12) for c in (0, 1, 3)]


def cmd_clk(out):
    cmd_rep(out, 3, CLK_CFGS)


AB_CFGS = [("I ocioso, camera ligada", {}, "none", 10), ("V video baseline", {}, "mjpeg", 12),
           ("O ocioso, camera desligada", dict(camoff=True), "none", 10)]


def cmd_ab(out):
    import os
    cmd_rep(out, int(os.environ.get("BLOCKS", 3)), AB_CFGS)


def cmd_summary(path):
    res = json.load(open(path))
    names = []
    for r in res:
        if r["name"] not in names:
            names.append(r["name"])
    cols = ["fps", "gap95", "stalls", "lat95", "ping_avg", "ping_p95"]
    med = lambda v: st.median(v) if v else None
    print(f"{'config (mediana [min..max], n)':46}" + "".join(f"{c:>20}" for c in cols))
    for n in names:
        rs = [r for r in res if r["name"] == n and not r.get("error")]
        line = f"{n[:45]:46}"
        for c in cols:
            v = [r[c] for r in rs if r.get(c) is not None]
            line += f"{(f'{med(v):.0f} [{min(v):.0f}..{max(v):.0f}]' if v else '-'):>20}"
        print(line + f"  n={len(rs)}")


def cmd_show(path):
    res = json.load(open(path))
    cols = ["fps", "kbps", "bpf", "gap50", "gap95", "gapmax", "stalls", "lat50", "lat95", "latmax",
            "ping_avg", "ping_p95", "ping_loss", "rx_mbps", "rssi"]
    print(f"{'teste':46}" + "".join(f"{c:>9}" for c in cols))
    for r in res:
        print(f"{r['name'][:45]:46}" + "".join(f"{str(r.get(c, '-')):>9}" for c in cols))


if __name__ == "__main__":
    cmds = dict(run=cmd_run, show=cmd_show, rep=cmd_rep, prio=cmd_prio, clk=cmd_clk, ab=cmd_ab, summary=cmd_summary)
    if len(sys.argv) < 3 or sys.argv[1] not in cmds:
        sys.exit(__doc__ + "\n  rep <saida.json> / summary <saida.json>: repeticoes intercaladas + medianas")
    cmds[sys.argv[1]](sys.argv[2])
