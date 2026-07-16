#!/usr/bin/env python3
"""
minros latency test — Vector3 ile RTT ölçer (best-effort).

CH_SEND'e Vector3 gönderir, CH_RECV'den 2× echo bekler; round-trip süresini ölçer.
Protokol minrospy ile yönetilir.

Kullanım:
    python3 minros_latency.py [PORT] [BAUD]

Varsayılan: /dev/ttyACM0 115200
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as c

from minrospy import Node
from minrospy.interfaces.geometry_msgs import Vector3

CH_SEND = 0  # cihaz unreliable sub
CH_RECV = 1  # cihaz unreliable pub (echo)

ROUNDS = 100000
TIMEOUT = 2.0  # saniye / tur


def approx(a: float, b: float, eps: float = 1e-3) -> bool:
    return abs(a - b) <= eps * max(1.0, abs(b))


def main():
    port, baud = c.parse_args()
    ser = c.open_serial(port, baud, timeout=0.05)

    node = Node()
    node.transport = c.make_transport(ser)

    box: list[Vector3] = []
    node.create_subscription(Vector3, CH_RECV, box.append)
    pub = node.create_publisher(Vector3, CH_SEND)

    print(f"{c.BOLD}minros latency test — {port} @ {baud} — {ROUNDS} tur{c.RESET}\n")
    time.sleep(0.1)  # Arduino reset süresi

    vectors = c.complex_vectors(ROUNDS)
    latencies = []
    errors = 0

    for i, (x, y, z) in enumerate(vectors):
        box.clear()
        ser.reset_input_buffer()

        t0 = time.monotonic()
        pub.publish(Vector3(x, y, z))
        ok_recv = c.spin_until(node, lambda: bool(box), TIMEOUT)
        t1 = time.monotonic()

        if not ok_recv:
            print(f"  tur {i + 1:3d}/{ROUNDS}  {c.RED}TIMEOUT{c.RESET}")
            errors += 1
            continue

        msg = box[-1]
        ok = approx(msg.x, x * 2) and approx(msg.y, y * 2) and approx(msg.z, z * 2)
        rtt_ms = (t1 - t0) * 1000.0
        latencies.append(rtt_ms)

        status = f"{c.GREEN}OK{c.RESET}" if ok else f"{c.RED}VERİ HATASI{c.RESET}"
        print(f"  tur {i + 1:3d}/{ROUNDS}  {rtt_ms:6.2f} ms  {status}")

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
