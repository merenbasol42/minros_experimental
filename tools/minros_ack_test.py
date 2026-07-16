#!/usr/bin/env python3
"""
minros ACK mekanizması testi.

İki test:
  1. ACK gönder    → firmware retransmit ETMEMELİ  (TIMEOUT_MS + margin sonra sessiz)
  2. ACK gönderme  → firmware retransmit ETMELİ    (aynı seq tekrar gelmeli)

Bu test, reliability'nin DUPLICATE retransmit'lerini gözlemlemesi gerektiğinden
ham RawNode kullanır (Reliable overlay otomatik ACK + dedup yapıp tam da test edileni
gizlerdi). Frame kurma/parse ve CRC minrospy core'a; seq ve ACK formatı ise
minrospy.reliability.protocol'a aittir — elle crc8/build_frame yoktur.

Kullanım:
    python3 minros_ack_test.py [PORT] [BAUD]
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as c

from minrospy import RawNode
from minrospy.reliability import protocol
from minrospy.interfaces.geometry_msgs import Vector3

CH_SEND = 2  # cihaz reliable sub
CH_RECV = 3  # cihaz reliable pub (echo)

RETRANSMIT_TIMEOUT_MS = 50   # firmware reliability TIMEOUT_MS
WAIT_MS = 150                # bekleme: timeout'un ~3 katı, güvenli marj

PASS = f"{c.GREEN}GEÇTİ{c.RESET}"
FAIL = f"{c.RED}BAŞARISIZ{c.RESET}"


class AckTester:
    def __init__(self, node: RawNode):
        self.node = node
        self._send_seq = 0
        # CH_RECV'den gelen ham frame'ler: (seq, user_bytes). Reliable veri
        # payload'ı [SEQ][user bytes] taşır; seq önekini burada ayıklıyoruz.
        self.recv: list[tuple[int, bytes]] = []
        node.subscribe(CH_RECV, self._on_recv)

    def _on_recv(self, payload: bytes) -> None:
        if len(payload) < 1:
            return
        self.recv.append((payload[0], payload[1:]))

    def send_reliable(self, x=1.0, y=2.0, z=3.0) -> int:
        """0x04'e seq önekli (reliable) Vector3 gönderir; kullanılan seq'i döner."""
        self._send_seq = (self._send_seq % 0xFE) + 1  # 0xFF sentinel'den kaçın
        self.node.publish(CH_SEND, Vector3(x, y, z).to_bytes(), head=bytes([self._send_seq]))
        return self._send_seq

    def send_ack(self, ch: int, seq: int) -> None:
        """CH249 üzerinden ACK gönderir: [RESP][ch][seq]."""
        buf = bytes((int(protocol.ResponseType.ACK), ch & 0xFF, seq & 0xFF))
        self.node.publish(protocol.ACK_CHANNEL_ID, buf)

    def await_recv(self, timeout_s: float) -> tuple[int, bytes] | None:
        """İlk CH_RECV frame'ini bekler."""
        self.recv.clear()
        if c.spin_until(self.node, lambda: bool(self.recv), timeout_s):
            return self.recv[0]
        return None


def main():
    port, baud = c.parse_args()
    ser = c.open_serial(port, baud, timeout=0.05)

    node = RawNode()
    node.transport = c.make_transport(ser)
    t = AckTester(node)

    print(f"{c.BOLD}minros ACK testi — {port} @ {baud}{c.RESET}\n")
    time.sleep(0.1)

    results = []

    # ── Test 1: ACK gönder → retransmit gelmemeli ────────────────────────────
    print(f"{c.CYAN}[TEST 1]{c.RESET} ACK gönderilince retransmit gelmemeli...")
    ser.reset_input_buffer()
    t.send_reliable()
    first = t.await_recv(1.0)
    if first is None:
        print("  İlk yanıt alınamadı — test atlandı.\n")
        results.append(None)
    else:
        recv_seq, _ = first
        t.send_ack(CH_RECV, recv_seq)        # ACK gönder
        t.recv.clear()
        c.spin_for(node, WAIT_MS / 1000)     # retransmit gözle
        extras = [s for s, _ in t.recv if s == recv_seq]
        if extras:
            print(f"  {FAIL} — {len(extras)} retransmit geldi (seq={recv_seq})")
            results.append(False)
        else:
            print(f"  {PASS} — {WAIT_MS}ms içinde retransmit gelmedi")
            results.append(True)
    print()

    # ── Test 2: ACK gönderme → retransmit gelmeli ────────────────────────────
    print(f"{c.CYAN}[TEST 2]{c.RESET} ACK gönderilmeyince retransmit gelmeli...")
    ser.reset_input_buffer()
    t.send_reliable()
    first = t.await_recv(1.0)
    if first is None:
        print("  İlk yanıt alınamadı — test atlandı.\n")
        results.append(None)
    else:
        recv_seq, _ = first
        t.recv.clear()
        c.spin_for(node, WAIT_MS / 1000)     # ACK göndermeden gözle
        extras = [s for s, _ in t.recv if s == recv_seq]
        if extras:
            print(f"  {PASS} — retransmit geldi (seq={recv_seq}, {len(extras)}x)")
            t.send_ack(CH_RECV, recv_seq)    # temizlik
            results.append(True)
        else:
            print(f"  {FAIL} — {WAIT_MS}ms içinde retransmit gelmedi")
            results.append(False)
    print()

    passed = sum(1 for r in results if r is True)
    total = sum(1 for r in results if r is not None)
    print(f"{c.BOLD}Sonuç: {passed}/{total} test geçti{c.RESET}")

    ser.close()


if __name__ == "__main__":
    main()
