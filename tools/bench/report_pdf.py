#!/usr/bin/env python3
"""Gera tools/bench/results/relatorio-video-wifi.pdf a partir dos JSONs das rodadas de teste.

  python3 tools/bench/report_pdf.py

Convenções visuais (método dataviz): uma só escala por gráfico, marcas finas, grade discreta, pontos
individuais visíveis (n pequeno), azul = série 1 e laranja = série 2 (paleta validada em modo claro),
texto sempre em tinta neutra e nunca na cor da série.
"""
import json
import statistics as st
import textwrap
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.lines import Line2D
from matplotlib.patches import FancyBboxPatch, Rectangle

HERE = Path(__file__).parent
RES = HERE / "results"
OUT = RES / "relatorio-video-wifi.pdf"

# --- tokens (modo claro, validados) ---
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK2 = "#52514e"
GRID = "#e4e3df"
BLUE = "#2a78d6"    # série 1
ORANGE = "#eb6834"  # série 2
GRAY = "#a3a29c"

plt.rcParams.update({
    "font.family": "DejaVu Sans", "font.size": 9, "text.color": INK, "axes.labelcolor": INK2,
    "xtick.color": INK2, "ytick.color": INK2, "axes.edgecolor": GRID, "axes.facecolor": SURFACE,
    "figure.facecolor": SURFACE, "axes.spines.top": False, "axes.spines.right": False,
    "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.6, "axes.axisbelow": True,
    "pdf.fonttype": 42,
})

PAGE = (11.69, 8.27)  # A4 paisagem


def load(name):
    return json.load(open(RES / name))


R1 = load("rodada1-matriz-unica.json")
R2 = load("rodada2-repeticoes.json")
R3 = load("rodada3-prioridade-cam.json")


def vals(rs, name_prefix, key):
    return [r[key] for r in rs if r["name"].startswith(name_prefix) and r.get(key) is not None and not r.get("error")]


def one(rs, name_prefix, key):
    v = vals(rs, name_prefix, key)
    return v[0] if v else None


def page(title, subtitle=None, foot=None):
    fig = plt.figure(figsize=PAGE)
    fig.text(0.05, 0.95, title, fontsize=17, fontweight="bold", va="top")
    if subtitle:
        fig.text(0.05, 0.912, subtitle, fontsize=10, color=INK2, va="top")
    fig.text(0.05, 0.012, foot or "Pantero · banco de testes de vídeo/Wi-Fi", fontsize=7.5, color=INK2)
    return fig


def wrap_text(fig, x, y, text, width=70, size=9.5, color=INK, dy=0.027, bold=False):
    for ln in textwrap.wrap(text, width):
        fig.text(x, y, ln, fontsize=size, color=color, va="top", fontweight="bold" if bold else "normal")
        y -= dy
    return y


def note(fig, y, text, fontsize=8.8, color=INK2):
    """Texto de rodapé com quebra automática (largura útil ~0,9 da página)."""
    width = int(150 * 8.8 / fontsize)
    return wrap_text(fig, 0.05, y, text, width, fontsize, color, dy=fontsize * 0.00265)


def dot_range(ax, y, values, color):
    """Pontos individuais + faixa mín–máx + marca da mediana (n pequeno: tudo visível)."""
    if not values:
        return
    ax.hlines(y, min(values), max(values), color=color, lw=2.2, alpha=0.45, zorder=2)
    ax.plot(values, [y] * len(values), "o", ms=5.5, color=color, mec=SURFACE, mew=1.2, zorder=3)
    ax.plot([st.median(values)], [y], marker="|", ms=16, mew=2.4, color=INK, zorder=4)


def legend_runs(ax, entries, loc="lower right"):
    h = [Line2D([], [], marker="o", ls="", color=c, mec=SURFACE, ms=6, label=l) for l, c in entries]
    h.append(Line2D([], [], marker="|", ls="", color=INK, ms=12, mew=2.2, label="mediana"))
    ax.legend(handles=h, loc=loc, frameon=False, fontsize=8, labelcolor=INK2)


# ================================================================== página 1: resumo
def p_summary(pdf):
    fig = page("Vídeo travado e latência alta: o que os testes mostram",
               "ESP32 (modo AP) + câmera OV2640 · 3 rodadas de teste · cliente: notebook a ~30 cm do ESP32")

    cards = [
        ("4–8 ms", "ping com a câmera DESLIGADA", BLUE),
        ("15–60 ms", "ping com a câmera LIGADA (mesmo ociosa)", ORANGE),
        ("< 3 %", "da capacidade do enlace usada pelo vídeo\n(0,5–1,6 Mbps de 58–72 Mbps)", INK2),
    ]
    for i, (big, small, col) in enumerate(cards):
        x0 = 0.05 + i * 0.315
        fig.add_artist(FancyBboxPatch((x0, 0.69), 0.295, 0.16, boxstyle="round,pad=0.004,rounding_size=0.01",
                                      fc="white", ec=GRID, lw=1, transform=fig.transFigure))
        fig.add_artist(Rectangle((x0 + 0.012, 0.715), 0.006, 0.11, fc=col, ec="none", transform=fig.transFigure))
        fig.text(x0 + 0.03, 0.80, big, fontsize=24, fontweight="bold", va="center")
        fig.text(x0 + 0.03, 0.735, small, fontsize=8.8, color=INK2, va="center")

    y = 0.63
    fig.text(0.05, y, "Conclusões", fontsize=12.5, fontweight="bold")
    y -= 0.04
    itens = [
        ("O rádio não é o gargalo.",
         "Sinal entre −31 e −47 dBm, enlace de 58 a 72 Mbps e vídeo usando menos de 3% disso."),
        ("A câmera ligada degrada o Wi-Fi do próprio ESP32.",
         "Mesmo sem ninguém assistindo, o ping sobe de 4–8 ms (câmera desligada) para 15–60 ms. "
         "Repetido em todas as rodadas; nas 4 repetições intercaladas as faixas não se sobrepõem."),
        ("Nenhum ajuste de vídeo foi comprovadamente melhor.",
         "Envio raw ou chunked, Nagle, JPEG, resolução, limite de fps, grab_mode, g/n, TX 8,5–21 dBm e WebSocket: "
         "as faixas de variação se sobrepõem ao baseline (15–25 fps)."),
        ("Hipótese descartada: prioridade da task da câmera.",
         "A cam_task roda no núcleo 0 com prioridade 23, igual à do Wi-Fi, mas baixá-la (23 → 2) não mudou o ping."),
        ("O ambiente de 2,4 GHz é ruim.",
         "O canal 6 do ESP32 está colado numa rede de 100% no canal 7 e noutra de 72% no canal 6."),
    ]
    for t, d in itens:
        fig.text(0.05, y, "•", fontsize=11, va="top")
        y = wrap_text(fig, 0.065, y, t, 90, 10.8, bold=True, dy=0.027)
        y = wrap_text(fig, 0.065, y + 0.002, d, 118, 9.8, color=INK2, dy=0.025) - 0.018
    pdf.savefig(fig)
    plt.close(fig)


# ================================================================== página 2: câmera liga/desliga
def p_camera(pdf):
    fig = page("A câmera ligada multiplica a latência do Wi-Fi",
               "Ping a cada 0,2 s no ESP32 ocioso (sem vídeo). Cada ponto é uma medição de 10 s; o traço preto é a mediana.")
    sets = [
        ("Rodada 1 (1 medição)", [one(R1, "Z2", "ping_avg")], [one(R1, "Z1", "ping_avg")],
         [one(R1, "Z2", "ping_p95")], [one(R1, "Z1", "ping_p95")]),
        ("Rodada 2 (4 medições)", vals(R2, "R1", "ping_avg"), vals(R2, "R0", "ping_avg"),
         vals(R2, "R1", "ping_p95"), vals(R2, "R0", "ping_p95")),
        ("Rodada 3 (só câmera ligada, 20)", [], vals(R3, "P", "ping_avg"), [], vals(R3, "P", "ping_p95")),
        ("Controle final (2 medições)", [8.5, 7.4], [37.2, 47.8], [20.4, 15.7], [98.3, 154.0]),
    ]
    for pi, (ttl, ioff, ion) in enumerate([("Ping médio (ms)", 1, 2), ("Ping p95 (ms)", 3, 4)]):
        ax = fig.add_axes([0.24, 0.17, 0.30, 0.64]) if pi == 0 else fig.add_axes([0.62, 0.17, 0.33, 0.64])
        ax.set_title(ttl, loc="left", fontsize=10.5, fontweight="bold", pad=10)
        for k, s in enumerate(sets):
            y = len(sets) - 1 - k
            dot_range(ax, y + 0.17, [v for v in s[ioff] if v is not None], BLUE)
            dot_range(ax, y - 0.17, [v for v in s[ion] if v is not None], ORANGE)
        ax.set_yticks(range(len(sets)))
        ax.set_yticklabels([s[0] for s in reversed(sets)] if pi == 0 else [], fontsize=8.5)
        ax.set_ylim(-0.6, len(sets) - 0.4)
        ax.set_xlim(0, 215 if pi else 100)
        ax.grid(axis="y", visible=False)
        ax.set_xlabel("ms (menos é melhor)")
        if pi == 0:
            legend_runs(ax, [("câmera desligada", BLUE), ("câmera ligada", ORANGE)], loc="upper right")
    note(fig, 0.118, "Leitura: com a câmera desligada o enlace fica em 4–8 ms, sempre. Com ela ligada a latência varia de 15 a 60 ms "
             "de uma medição para outra: o efeito é real e repetido, mas a magnitude oscila.", fontsize=8.8, color=INK2)
    note(fig, 0.068, "O controle final foi medido durante a sessão e não está nos JSONs; as demais medições estão em tools/bench/results/.",
             fontsize=7.8, color=INK2)
    pdf.savefig(fig)
    plt.close(fig)


# ================================================================== página 3: configs de vídeo
def p_video(pdf):
    fig = page("Configurações de vídeo: as diferenças ficam dentro do ruído",
               "Rodada 2: 4 repetições intercaladas e embaralhadas por configuração (12 s cada). Ponto = medição; traço = mediana.")
    cfg = [
        ("V0", "V0  baseline (chunked, QVGA q15, b/g/n)"),
        ("V1", "V1  grab_mode = WHEN_EMPTY"),
        ("V2", "V2  só 802.11 g/n (sem 11b)"),
        ("V3", "V3  WHEN_EMPTY + g/n"),
        ("V4", "V4  WHEN_EMPTY + g/n + envio raw"),
        ("V5", "V5  WHEN_EMPTY + g/n + QQVGA"),
        ("W1", "W1  WebSocket binário"),
        ("W2", "W2  WebSocket + WHEN_EMPTY + g/n"),
    ]
    panels = [("Quadros por segundo (fps)", "fps", "mais é melhor", 0, 38),
              ("Atraso relativo p95 (ms)", "lat95", "menos é melhor", 0, 700),
              ("Ping de controle médio (ms)", "ping_avg", "menos é melhor", 0, 130)]
    for pi, (ttl, key, hint, lo, hi) in enumerate(panels):
        ax = fig.add_axes([0.31 + pi * 0.225, 0.17, 0.19, 0.64])
        ax.set_title(ttl, loc="left", fontsize=9.5, fontweight="bold", pad=10)
        b = vals(R2, "V0", key)
        if b:
            ax.axvspan(min(b), max(b), color=ORANGE, alpha=0.09, lw=0)
        for i, (p, lbl) in enumerate(cfg):
            dot_range(ax, len(cfg) - 1 - i, vals(R2, p, key), ORANGE if p == "V0" else BLUE)
        ax.set_yticks(range(len(cfg)))
        ax.set_yticklabels([c[1] for c in reversed(cfg)] if pi == 0 else [], fontsize=8)
        ax.set_ylim(-0.6, len(cfg) - 0.4)
        ax.set_xlim(lo, hi)
        ax.grid(axis="y", visible=False)
        ax.set_xlabel(hint)
    hs = [Line2D([], [], marker="o", ls="", color=ORANGE, mec=SURFACE, ms=6, label="baseline"),
          Line2D([], [], marker="o", ls="", color=BLUE, mec=SURFACE, ms=6, label="variações"),
          Line2D([], [], marker="|", ls="", color=INK, ms=12, mew=2.2, label="mediana")]
    fig.legend(handles=hs, loc="upper right", bbox_to_anchor=(0.95, 0.885), frameon=False, fontsize=8.5,
               labelcolor=INK2, ncol=3)
    note(fig, 0.118, "Leitura: a faixa laranja é a variação do baseline. Quase todas as variações a cobrem: sem evidência de ganho. "
             "Os melhores medianos (W2, V5, V4) também têm a maior dispersão (V5: 6 a 32 fps).", fontsize=8.8, color=INK2)
    note(fig, 0.068, "Atraso relativo = (chegada no PC − timestamp de captura no ESP32) menos o melhor caso da própria medição; "
             "não é latência absoluta (relógios não sincronizados). Valores acima de 700 ms ficam fora do eixo "
         f"(V5 chegou a {max(vals(R2, 'V5', 'lat95')):.0f} ms e W2 a {max(vals(R2, 'W2', 'lat95')):.0f} ms).", fontsize=7.8, color=INK2)
    pdf.savefig(fig)
    plt.close(fig)


# ================================================================== página 4: prioridade da cam_task
def p_prio(pdf):
    fig = page("Hipótese testada e descartada: prioridade da task da câmera",
               "A cam_task roda no núcleo 0 com prioridade 23 (mesma do Wi-Fi). Rodada 3: 4 repetições por prioridade, ordem embaralhada.")
    prios_idle = [23, 17, 10, 5, 2]
    prios_vid = [23, 10, 5, 2]
    ax = fig.add_axes([0.17, 0.20, 0.30, 0.58])
    ax.set_title("Ping médio com a câmera ociosa (ms)", loc="left", fontsize=10, fontweight="bold", pad=10)
    for i, p in enumerate(prios_idle):
        dot_range(ax, len(prios_idle) - 1 - i, vals(R3, f"P{p} ", "ping_avg"), ORANGE)
    ax.set_yticks(range(len(prios_idle)))
    ax.set_yticklabels([f"prioridade {p}" + ("  (padrão)" if p == 23 else "") for p in reversed(prios_idle)])
    ax.set_xlim(0, 100)
    ax.set_xlabel("menos é melhor")
    ax.grid(axis="y", visible=False)
    ref = st.mean(vals(R2, "R1", "ping_avg"))
    ax.axvline(ref, color=BLUE, lw=1.4, ls=(0, (4, 3)))
    ax.set_ylim(-0.6, len(prios_idle) - 0.4)
    ax.text(ref + 1.5, -0.55, "câmera desligada (rodada 2)", color=INK2, fontsize=8, va="bottom")

    ax2 = fig.add_axes([0.66, 0.20, 0.28, 0.58])
    ax2.set_title("Quadros por segundo com vídeo (fps)", loc="left", fontsize=10, fontweight="bold", pad=10)
    for i, p in enumerate(prios_vid):
        dot_range(ax2, len(prios_vid) - 1 - i, vals(R3, f"PV{p} ", "fps"), ORANGE)
    ax2.set_yticks(range(len(prios_vid)))
    ax2.set_yticklabels([f"prioridade {p}" + ("  (padrão)" if p == 23 else "") for p in reversed(prios_vid)])
    ax2.set_xlim(0, 30)
    ax2.set_xlabel("mais é melhor")
    ax2.grid(axis="y", visible=False)
    note(fig, 0.118, "Leitura: nenhuma prioridade melhorou o ping nem o fps. O custo da câmera não vem da prioridade da task; "
             "restam como hipóteses a disputa por PSRAM/cache e a carga de interrupções do DMA (não testadas).", fontsize=8.8, color=INK2)
    note(fig, 0.068, "Nesta rodada o ambiente estava pior que na rodada 2 (fps do baseline caiu de ~18 para ~4): os níveis absolutos não são comparáveis entre rodadas.",
             fontsize=7.8, color=INK2)
    pdf.savefig(fig)
    plt.close(fig)


# ================================================================== página 5: matriz de 1 medição
def p_matrix(pdf):
    fig = page("Rodada 1: uma medição por configuração (baixa confiança)",
               "A faixa laranja é a variação do próprio baseline (3 medições: 15,5 a 25,1 fps). Barras dentro dela não indicam efeito.")
    rows = [
        ("baseline (média de 3)", None),
        ("envio raw (1 send/frame)", "B1"), ("sem TCP_NODELAY (Nagle)", "B2"), ("raw + sem NODELAY", "B3"),
        ("JPEG q10", "C QVGA q10"), ("JPEG q25", "C QVGA q25"), ("JPEG q40", "C QVGA q40"),
        ("160×120 (QQVGA)", "D1"), ("240×176 (HQVGA)", "D2"), ("352×288 (CIF)", "D3"), ("640×480 (VGA)", "D4"),
        ("limite 10 fps", "E limite 10"), ("limite 15 fps", "E limite 15"), ("limite 20 fps", "E limite 20"),
        ("captura sob demanda", "E4"),
        ("canal 40 MHz (HT40)", "F1"), ("só g/n", "F2"), ("só n", "F3"),
        ("TX 8,5 dBm", "F tx 8.5"), ("TX 15 dBm", "F tx 15"), ("TX 21 dBm", "F tx 21"),
        ("xclk 10 MHz", "G1"), ("grab WHEN_EMPTY", "G2"), ("3 buffers de frame", "G5"),
        ("WebSocket (fila de 32)", "H1"),
    ]
    base = [r for r in R1 if r["name"].startswith("A")]
    bf = [r["fps"] for r in base]
    ba = [r["ping_avg"] for r in base]
    data = []
    for lbl, pref in rows:
        if pref is None:
            data.append((lbl, st.mean(bf), st.mean(ba)))
        else:
            r = next((x for x in R1 if x["name"].startswith(pref)), None)
            data.append((lbl, r["fps"], r["ping_avg"]) if r else (lbl, None, None))
    n = len(data)
    for pi, (ttl, idx, band, hi) in enumerate([("Quadros por segundo (fps)", 1, bf, 40),
                                               ("Ping de controle médio (ms)", 2, ba, 140)]):
        ax = fig.add_axes([0.22, 0.17, 0.34, 0.65]) if pi == 0 else fig.add_axes([0.62, 0.17, 0.32, 0.65])
        ax.set_title(ttl, loc="left", fontsize=9.5, fontweight="bold", pad=8)
        ax.axvspan(min(band), max(band), color=ORANGE, alpha=0.13, lw=0)
        for i, d in enumerate(data):
            if d[idx] is not None:
                ax.barh(n - 1 - i, d[idx], height=0.55, color=ORANGE if i == 0 else BLUE, zorder=3)
        ax.set_yticks(range(n))
        ax.set_yticklabels([d[0] for d in reversed(data)] if pi == 0 else [], fontsize=7.6)
        ax.set_ylim(-0.7, n - 0.3)
        ax.set_xlim(0, hi)
        ax.grid(axis="y", visible=False)
    note(fig, 0.122, "Isolados: JPEG q10 caiu para 3,5 fps e o VGA para 5 fps; HT40 piorou (9 fps, ping 122 ms). Confirmar com repetições antes de agir. "
             "Testes de frame buffer em DRAM descartados (a câmera não entregou frames).", fontsize=7.8, color=INK2)
    pdf.savefig(fig)
    plt.close(fig)


# ================================================================== página 6: canais Wi-Fi
def p_channels(pdf):
    fig = page("Ambiente de rádio: o canal 6 está num trecho congestionado",
               "Redes de 2,4 GHz vistas pelo notebook durante os testes (intensidade de sinal, 0–100), uma barra por rede, na posição do canal.")
    ax = fig.add_axes([0.08, 0.17, 0.86, 0.60])
    nets = [(2, 54), (3, 70), (6, 72), (7, 100), (10, 100), (11, 55), (11, 55), (11, 50)]
    seen = {}
    for ch, sig in nets:
        k = seen.get(ch, 0)
        seen[ch] = k + 1
        n_at = sum(1 for c, _ in nets if c == ch)
        ax.bar(ch + (k - (n_at - 1) / 2) * 0.28 - (0.16 if ch == 6 else 0), sig, width=0.26, color=GRAY, zorder=3)
    ax.bar(6.16, 99, width=0.26, color=ORANGE, zorder=4)
    ax.annotate("Pantero (ESP32), canal 6", (6.16, 99), xytext=(3.4, 108), fontsize=9, color=INK,
                arrowprops=dict(arrowstyle="-", color=INK2, lw=0.8))
    ax.annotate("rede de 100% no canal 7:\nsobrepõe o canal 6", (7, 100), xytext=(7.6, 112), fontsize=8.5, color=INK2,
                arrowprops=dict(arrowstyle="-", color=INK2, lw=0.8))
    ax.annotate("rede de 72% no mesmo canal", (5.84, 72), xytext=(2.6, 84), fontsize=8.5, color=INK2,
                arrowprops=dict(arrowstyle="-", color=INK2, lw=0.8))
    ax.set_xticks(range(1, 14))
    ax.set_xlim(0.4, 13.6)
    ax.set_ylim(0, 128)
    ax.set_xlabel("canal Wi-Fi 2,4 GHz (cada canal tem 20 MHz de largura e se sobrepõe aos vizinhos próximos)")
    ax.set_ylabel("sinal (0–100)")
    ax.grid(axis="x", visible=False)
    ax.legend(handles=[Line2D([], [], marker="s", ls="", color=ORANGE, label="Pantero"),
                       Line2D([], [], marker="s", ls="", color=GRAY, label="redes vizinhas")],
              loc="upper right", frameon=False, fontsize=8.5, labelcolor=INK2)
    note(fig, 0.118, "Leitura: trocar de canal ainda não foi testado; os canais 1 e 11 são candidatos (o ESP32 não opera em 5 GHz). "
             "Com a câmera desligada o ping fica em 4–8 ms neste mesmo ambiente, então a interferência sozinha não explica os atrasos.",
             fontsize=8.6, color=INK2)
    pdf.savefig(fig)
    plt.close(fig)


# ================================================================== página 7: recomendações
def p_reco(pdf):
    fig = page("Recomendações", "Ordenadas por evidência e custo. Nada abaixo foi aplicado ao firmware de produção.")
    recs = [
        ("1", "Ligar a câmera só quando houver espectador",
         "Evidência forte e repetida: ping ocioso de 28–60 ms (câmera ligada) para 4–8 ms (desligada). "
         "Custo: ~0,3 s para iniciar a câmera ao abrir o vídeo. Não ajuda enquanto o vídeo está rodando."),
        ("2", "Testar outro canal Wi-Fi (1 ou 11) mantendo 20 MHz",
         "O canal 6 está sobreposto a uma rede de 100% (canal 7) e a outra de 72%. Teste simples: mudar o número em WiFi.softAP e repetir "
         "a medição. Evitar 40 MHz (piorou numa medição isolada)."),
        ("3", "Medir com o celular que será usado, longe do ESP32",
         "Os testes foram com notebook a ~30 cm (sinal −31 a −47 dBm). Em distância real a potência de TX e o canal passam a pesar; "
         "perto, não fazem diferença."),
        ("4", "Experimentos para achar a causa do custo da câmera",
         "Não testados: reduzir o fps do sensor (registrador CLKRC), frame buffer em DRAM com inicialização válida, medir carga de CPU por "
         "núcleo. Hipóteses: disputa de PSRAM/cache e interrupções do DMA da câmera."),
        ("5", "Não insistir nas otimizações já tentadas",
         "Compressão adaptativa, envio direto no socket, Nagle, potência de TX e prioridade da task não mostraram ganho fora do ruído."),
        ("6", "Se ainda for pouco: separar funções em hardware",
         "Um ESP32 clássico fazendo câmera, AP e controle ao mesmo tempo tem pouca folga. Uma placa só para a câmera (ou um ESP32-S3) "
         "tira a disputa pela raiz."),
    ]
    y = 0.86
    for n, t, d in recs:
        fig.add_artist(FancyBboxPatch((0.05, y - 0.095), 0.9, 0.105, boxstyle="round,pad=0.002,rounding_size=0.008",
                                      fc="white", ec=GRID, lw=1, transform=fig.transFigure))
        fig.text(0.07, y - 0.045, n, fontsize=18, fontweight="bold", color=ORANGE if n == "1" else INK2, va="center")
        fig.text(0.105, y - 0.012, t, fontsize=10.3, fontweight="bold", va="top")
        wrap_text(fig, 0.105, y - 0.038, d, 135, 8.6, color=INK2, dy=0.021)
        y -= 0.122
    note(fig, 0.075, "Limitações: n = 4 por configuração; latência relativa (não absoluta); cliente = notebook; ambiente de rádio variou durante a sessão.",
             fontsize=8, color=INK2)
    pdf.savefig(fig)
    plt.close(fig)


def main():
    RES.mkdir(exist_ok=True)
    with PdfPages(OUT) as pdf:
        for fn in (p_summary, p_camera, p_video, p_prio, p_matrix, p_channels, p_reco):
            fn(pdf)
        d = pdf.infodict()
        d["Title"] = "Pantero: relatório de testes de vídeo e Wi-Fi"
        d["Author"] = "banco de testes tools/bench"
    print("gerado:", OUT)


if __name__ == "__main__":
    main()
