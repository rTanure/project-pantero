"""Testes do protocolo UDP (remontagem e FEC do vídeo, métricas, avisos de reset, controlador adaptativo)
e do fluxo completo com o robô simulado.

  python3 -m unittest discover -s pc/tests -v
"""
import os
import struct
import sys
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fake_robot import FakeRobot  # noqa: E402
from pantero.controller import LEVELS, AdaptiveController  # noqa: E402
from pantero.link import RobotLink  # noqa: E402
from pantero.resets import ResetTracker, detect_reboot, parse_reset_message, should_alert  # noqa: E402
from pantero.settings import Settings  # noqa: E402
from pantero.video import FrameAssembler, VideoReceiver  # noqa: E402

CHUNK = 1400


def packets(frame_id: int, jpeg: bytes, size: int = CHUNK, ts: int = 1000, fec: bool = False):
    """Pacotes de um quadro, no formato do firmware (cabeçalho de 8 bytes; com FEC, o pacote de paridade no fim)."""
    count = (len(jpeg) + size - 1) // size
    out = [struct.pack(">HBBI", frame_id, i, count, ts) + jpeg[i * size:(i + 1) * size] for i in range(count)]
    if fec:
        parity = bytearray(size)
        for i in range(count):
            for k, byte in enumerate(jpeg[i * size:(i + 1) * size]):
                parity[k] ^= byte
        out.append(struct.pack(">HBBI", frame_id, count, count, ts) + struct.pack(">I", len(jpeg)) + bytes(parity))
    return out


def feed_all(assembler, pkts):
    result = None
    for p in pkts:
        result = assembler.feed(p) or result
    return result


class FrameAssemblerTest(unittest.TestCase):
    def setUp(self):
        self.jpeg = bytes(range(256)) * 20  # 5120 bytes = 4 pacotes (o último incompleto)
        self.assembler = FrameAssembler()

    def test_pacotes_em_ordem_fecham_o_quadro(self):
        frame = feed_all(self.assembler, packets(7, self.jpeg, ts=4321))
        self.assertEqual(frame.jpeg, self.jpeg)
        self.assertEqual(frame.ts_ms, 4321)

    def test_pacotes_fora_de_ordem_dentro_do_quadro(self):
        pkts = packets(7, self.jpeg)
        results = [self.assembler.feed(p) for p in (pkts[2], pkts[0], pkts[3], pkts[1])]
        self.assertEqual(results[-1].jpeg, self.jpeg)
        self.assertTrue(all(r is None for r in results[:-1]))

    def test_quadro_com_pacote_perdido_e_descartado(self):
        for p in packets(1, self.jpeg)[:3]:  # falta o último pedaço
            self.assertIsNone(self.assembler.feed(p))
        self.assertEqual(feed_all(self.assembler, packets(2, self.jpeg)).jpeg, self.jpeg)
        self.assertEqual(self.assembler.dropped, 1)

    def test_pacote_atrasado_de_quadro_velho_e_ignorado(self):
        for p in packets(5, self.jpeg)[:2]:
            self.assembler.feed(p)
        for p in packets(6, self.jpeg):
            self.assembler.feed(p)
        self.assertIsNone(self.assembler.feed(packets(5, self.jpeg)[2]))

    def test_pacote_duplicado_depois_de_completo_e_ignorado(self):
        pkts = packets(3, self.jpeg)
        for p in pkts:
            self.assembler.feed(p)
        self.assertIsNone(self.assembler.feed(pkts[0]))

    def test_id_do_quadro_da_a_volta_em_16_bits(self):
        for fid in (65534, 65535, 0, 1):
            self.assertEqual(feed_all(self.assembler, packets(fid, self.jpeg)).jpeg, self.jpeg, f"quadro {fid}")

    def test_pacotes_invalidos_nao_quebram(self):
        for bad in (b"", b"abc", struct.pack(">HBBI", 1, 9, 2, 0) + b"x", struct.pack(">HBBI", 1, 0, 0, 0) + b"x"):
            self.assertIsNone(self.assembler.feed(bad))


class FecTest(unittest.TestCase):
    def setUp(self):
        self.jpeg = bytes((i * 7) % 256 for i in range(5120))  # 4 pedaços, o último com 120 bytes
        self.assembler = FrameAssembler()

    def test_recupera_qualquer_pedaco_perdido(self):
        count = len(packets(1, self.jpeg))
        for lost in range(count):  # inclui o último pedaço (mais curto)
            assembler = FrameAssembler()
            pkts = packets(10 + lost, self.jpeg, fec=True)
            data_pkts = [p for i, p in enumerate(pkts[:count]) if i != lost]
            frame = feed_all(assembler, data_pkts + [pkts[count]])
            self.assertIsNotNone(frame, f"pedaco {lost} nao recuperado")
            self.assertEqual(frame.jpeg, self.jpeg, f"pedaco {lost}")
            self.assertEqual(assembler.recovered, 1)

    def test_paridade_antes_dos_dados_tambem_recupera(self):
        pkts = packets(3, self.jpeg, fec=True)
        order = [pkts[-1], pkts[0], pkts[2], pkts[3]]  # falta o pedaço 1
        self.assertEqual(feed_all(self.assembler, order).jpeg, self.jpeg)

    def test_dois_pedacos_perdidos_nao_recupera(self):
        pkts = packets(4, self.jpeg, fec=True)
        self.assertIsNone(feed_all(self.assembler, [pkts[0], pkts[3], pkts[4]]))

    def test_sem_perda_nao_conta_recuperacao(self):
        feed_all(self.assembler, packets(5, self.jpeg, fec=True))
        self.assertEqual(self.assembler.recovered, 0)


class ResetsTest(unittest.TestCase):
    def test_parse(self):
        info = parse_reset_message("R,9,BROWNOUT,1850")
        self.assertEqual((info.board, info.label, info.code, info.name, info.uptime_ms, info.abnormal),
                         ("esp", "ESP32", 9, "BROWNOUT", 1850, True))
        self.assertFalse(parse_reset_message("U,0,BOOTLOADER,100").abnormal)
        for bad in ("", "X,1,A,2", "R,1,A", "R,a,B,2", "R,1,B,x"):
            self.assertIsNone(parse_reset_message(bad), bad)

    def test_reinicio_detectado_pelo_uptime(self):
        self.assertFalse(detect_reboot(None, 5000, 10_000))
        self.assertFalse(detect_reboot((5000, 10_000), 6100, 11_000))   # +1,1 s em 1 s: normal
        self.assertTrue(detect_reboot((11_000, 10_000), 3_600, 15_000))  # uptime voltou para trás

    def test_alerta_so_para_reset_anormal_recente(self):
        brownout_recente = parse_reset_message("R,9,BROWNOUT,5000")
        brownout_antigo = parse_reset_message("R,9,BROWNOUT,900000")
        poweron = parse_reset_message("R,1,POWERON,5000")
        self.assertTrue(should_alert(brownout_recente, False))
        self.assertFalse(should_alert(brownout_antigo, False))
        self.assertTrue(should_alert(brownout_antigo, True))   # reiniciou com o programa aberto
        self.assertFalse(should_alert(poweron, True))

    def test_tracker_so_marca_novidade(self):
        tracker = ResetTracker()
        first = tracker.update("U,0,BOOTLOADER,1000", 0)
        again = tracker.update("U,0,BOOTLOADER,2000", 1000)   # repetição a cada olá: sem novidade
        rebooted = tracker.update("U,0,BOOTLOADER,300", 2000)
        self.assertTrue(first.changed)
        self.assertFalse(again.changed)
        self.assertTrue(rebooted.changed and rebooted.rebooted)
        self.assertIsNone(tracker.update("lixo", 0))


class SettingsTest(unittest.TestCase):
    def test_padrao_tem_pacing(self):
        self.assertEqual(Settings().pace_us, 4000)
        self.assertEqual(Settings().hello_params(), "q=15,fs=2,fec=0,pace=4000,cam=0,rate=0")

    def test_formato_do_ola(self):
        s = Settings(quality=22, frame_size=1, fec=True, pace_us=500, cam_mode=1, wifi_rate=2)
        self.assertEqual(s.hello_params(), "q=22,fs=1,fec=1,pace=500,cam=1,rate=2")


class ControllerTest(unittest.TestCase):
    def setUp(self):
        self.settings = Settings(quality=15, frame_size=2)
        self.controller = AdaptiveController(self.settings)
        self.start_level = self.controller.level
        self.now = 100.0

    def tick(self, loss=0.0, latency=50.0, recv=20.0, sent=20.0):
        self.now += 1.0
        return self.controller.update(loss, latency, recv, sent, self.now)

    def test_rede_ruim_reduz_a_qualidade_so_depois_de_2_ciclos(self):
        self.assertIsNone(self.tick(loss=20))
        self.assertIsNotNone(self.tick(loss=20))
        self.assertEqual(self.controller.level, self.start_level + 1)
        self.assertEqual((self.settings.frame_size, self.settings.quality), LEVELS[self.start_level + 1])

    def test_atraso_alto_tambem_reduz(self):
        self.tick(latency=400)
        self.assertIsNotNone(self.tick(latency=400))

    def test_recebendo_bem_menos_que_o_enviado_reduz(self):
        self.tick(recv=8, sent=20)
        self.assertIsNotNone(self.tick(recv=8, sent=20))

    def test_rede_folgada_so_melhora_devagar(self):
        self.tick(loss=20)
        self.tick(loss=20)  # piorou um nível
        level = self.controller.level
        for _ in range(5):
            self.assertIsNone(self.tick())
        self.assertIsNotNone(self.tick())  # 6 ciclos bons seguidos e já passou o tempo mínimo
        self.assertEqual(self.controller.level, level - 1)

    def test_ciclo_ruim_zera_a_contagem_de_bons(self):
        self.tick(loss=20)
        self.tick(loss=20)
        for _ in range(4):
            self.tick()
        self.tick(loss=20)  # interrompe a sequência de bons
        for _ in range(3):
            self.assertIsNone(self.tick())

    def test_nao_passa_dos_limites_da_escada(self):
        for _ in range(60):
            self.tick(loss=50)
        self.assertEqual(self.controller.level, len(LEVELS) - 1)
        for _ in range(200):
            self.tick()
        self.assertEqual(self.controller.level, 0)


class EndToEndTest(unittest.TestCase):
    """Programa do PC (enlace + vídeo) contra o robô simulado, em localhost."""

    def setUp(self):
        self.robot = FakeRobot(control_port=0, fps=20, loss=0.0)
        self.robot.start()
        self.video = VideoReceiver(port=0)
        self.video.start()
        self.messages = []
        self.command = (0, 0)
        self.settings = Settings()
        self.link = RobotLink(lambda: self.command, self.messages.append, self.settings, "127.0.0.1",
                              self.robot.control_port, self.video.port)
        self.link.start()

    def tearDown(self):
        self.link.stop()
        self.video.stop()
        self.robot.stop()

    def wait(self, condition, timeout=6.0):
        end = time.time() + timeout
        while time.time() < end:
            if condition():
                return True
            time.sleep(0.05)
        return False

    def test_conecta_recebe_avisos_video_e_envia_comandos(self):
        self.assertTrue(self.wait(lambda: self.link.connected), "nao conectou")
        self.assertTrue(any(m.startswith("R,") for m in self.messages))
        self.assertTrue(any(m.startswith("U,") for m in self.messages))
        self.assertFalse(any(m.startswith(("S,", "P,")) for m in self.messages), "S e P sao tratados no enlace")

        self.assertTrue(self.wait(lambda: self.video.latest()[0] >= 5), "nao recebeu video")
        _, image = self.video.latest()
        self.assertEqual(image.size, (320, 240))

        self.command = (20, 30)
        self.assertTrue(self.wait(lambda: "20,30" in self.robot.commands), "comando nao chegou")
        self.command = (0, 0)
        self.assertTrue(self.wait(lambda: self.robot.commands[-1] == "0,0"), "parada final nao chegou")

    def test_parado_nao_inunda_o_robo_com_comandos(self):
        self.assertTrue(self.wait(lambda: self.link.connected))
        time.sleep(1.0)
        self.assertLessEqual(len([c for c in self.robot.commands if c == "0,0"]), 2)

    def test_ajustes_chegam_ao_robo_e_o_estado_volta(self):
        self.assertTrue(self.wait(lambda: self.link.connected))
        self.settings.quality = 30
        self.settings.fec = True
        self.settings.pace_us = 500
        self.settings.frame_size = 1
        self.assertTrue(self.wait(lambda: self.robot.settings["q"] == 30 and self.robot.settings["fec"] == 1
                                  and self.robot.settings["pace"] == 500 and self.robot.settings["fs"] == 1),
                        "ajustes nao chegaram")
        self.assertTrue(self.wait(lambda: self.link.esp_status.get("q") == 30), "estado S nao voltou")
        self.assertTrue(self.wait(lambda: self.video.latest()[1] is not None and self.video.latest()[1].size == (240, 176)),
                        "a imagem nao mudou para a nova resolucao")

    def test_ping_mede_o_tempo_de_ida_e_volta(self):
        self.assertTrue(self.wait(lambda: self.link.rtt_stats()[0] is not None), "sem medicao de ping")
        avg, p95 = self.link.rtt_stats()
        self.assertLess(avg, 100)
        self.assertGreaterEqual(p95, 0)

    def test_metricas_do_video(self):
        self.assertTrue(self.wait(lambda: self.video.latest()[0] >= 20), "poucos quadros")
        stats = self.video.stats()
        self.assertGreater(stats.fps, 5)
        self.assertGreater(stats.bitrate_mbps, 0.05)
        self.assertGreater(stats.frame_kb, 1)
        self.assertIsNotNone(stats.latency_p95_ms)
        self.assertLess(stats.latency_p95_ms, 500)
        self.assertIsNotNone(stats.jitter_p95_ms)
        self.assertLess(stats.loss_pct, 5)

    def test_fec_recupera_quadros_com_perda(self):
        self.robot._loss = 0.0
        self.assertTrue(self.wait(lambda: self.link.connected))
        self.robot._loss = 0.08
        self.settings.fec = True
        self.assertTrue(self.wait(lambda: self.video.stats().recovered >= 2, timeout=12), "FEC nao recuperou nada")


class EndToEndWithLossTest(unittest.TestCase):
    def test_com_perda_de_pacotes_so_mostra_quadros_inteiros(self):
        robot = FakeRobot(control_port=0, fps=30, loss=0.15)
        robot.start()
        video = VideoReceiver(port=0)
        video.start()
        link = RobotLink(lambda: (0, 0), lambda _m: None, Settings(), "127.0.0.1", robot.control_port, video.port)
        link.start()
        try:
            end = time.time() + 6
            while time.time() < end and video.latest()[0] < 10:
                time.sleep(0.05)
            self.assertGreaterEqual(video.latest()[0], 10, "poucos quadros com 15% de perda")
            self.assertGreater(video.dropped, 0, "com perda deveria ter descartado quadros")
            self.assertGreater(video.stats().loss_pct, 0)
            self.assertEqual(video.latest()[1].size, (320, 240))
        finally:
            link.stop()
            video.stop()
            robot.stop()


if __name__ == "__main__":
    unittest.main()
