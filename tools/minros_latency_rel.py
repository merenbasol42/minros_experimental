#!/usr/bin/env python3
"""
minros reliable latency test — CH_SEND → CH_RECV, güvenilir kanalda RTT.

Reliable publisher/subscriber minrospy'nin reliability overlay'i ile yönetilir:
giden mesajın ACK'i ve gelen echo'nun ACK'i otomatiktir; seq/dedup/retransmit
gizlidir. Elle ACK/seq kurma yoktur.

Kullanım:
    python3 minros_latency_rel.py [PORT] [BAUD]

Varsayılan: /dev/ttyACM0 9600
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as c

from minrospy import Node
from minrospy.interfaces.geometry_msgs import Vector3

CH_SEND = 2  # cihaz reliable sub
CH_RECV = 3  # cihaz reliable pub (echo)

ROUNDS = 100000
TIMEOUT = 2.0


def approx(a: float, b: float, eps: float = 1e-3) -> bool:
    return abs(a - b) <= eps * max(1.0, abs(b))


def main():
    port, baud = c.parse_args(default_baud=9600)
    ser = c.open_serial(port, baud, timeout=0.05)

    node = Node()
    node.transport = c.make_transport(ser)

    box: list[Vector3] = []
    # reliable subscriber: gelen echo'ya otomatik ACK + dedup, callback temiz mesaj alır
    node.create_subscription(Vector3, CH_RECV, box.append, reliable=True)
    pub = node.create_publisher(Vector3, CH_SEND, reliable=True)

    print(f"{c.BOLD}minros reliable latency test — {port} @ {baud} — {ROUNDS} tur{c.RESET}\n")
    time.sleep(0.1)

    vectors = c.complex_vectors(ROUNDS)
    latencies = []
    errors = 0

    for i, (x, y, z) in enumerate(vectors):
        msg = Vector3(x, y, z)
        box.clear()

        # Reliable publish: önceki gönderim ACK'lenmediyse pub.publish False döner;
        # ACK gelene kadar (overlay tick eder) kısa süre dene.
        if not pub.publish(msg) and not c.spin_until(node, lambda: pub.publish(msg), TIMEOUT):
            print(f"  tur {i + 1:5d}/{ROUNDS}  {c.RED}TIMEOUT (gönderilemedi){c.RESET}")
            errors += 1
            continue

        t0 = time.monotonic()
        ok_recv = c.spin_until(node, lambda: bool(box), TIMEOUT)
        t1 = time.monotonic()

        if not ok_recv:
            print(f"  tur {i + 1:5d}/{ROUNDS}  {c.RED}TIMEOUT{c.RESET}")
            errors += 1
            continue

        m = box[-1]
        ok = approx(m.x, x * 2) and approx(m.y, y * 2) and approx(m.z, z * 2)
        rtt_ms = (t1 - t0) * 1000.0
        latencies.append(rtt_ms)

        status = f"{c.GREEN}OK{c.RESET}" if ok else f"{c.RED}VERİ HATASI{c.RESET}"
        print(f"  tur {i + 1:5d}/{ROUNDS}  {rtt_ms:6.2f} ms  {status}")

    print()
    if latencies:
        avg = sum(latencies) / len(latencies)
        print(f"{c.BOLD}Sonuçlar ({len(latencies)}/{ROUNDS} başarılı):{c.RESET}")
        print(f"  {c.CYAN}Ortalama : {avg:.2f} ms{c.RESET}")
        print(f"  Min      : {min(latencies):.2f} ms")
        print(f"  Max      : {max(latencies):.2f} ms")
        if errors:
            print(f"  {c.RED}Kayıp    : {errors}{c.RESET}")
    else:
        print(f"{c.RED}Hiçbir yanıt alınamadı.{c.RESET}")

    ser.close()


if __name__ == "__main__":
    main()
