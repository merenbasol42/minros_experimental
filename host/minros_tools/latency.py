#!/usr/bin/env python3
"""
minros latency test — Vector3 ile RTT ölçer (best-effort).

CH_SEND'e Vector3 gönderir, CH_RECV'den 2× echo bekler; round-trip süresini ölçer.
Protokol minrospy ile yönetilir.

Kullanım:
    minros-latency [PORT] [BAUD]

Varsayılan: /dev/ttyACM0 115200
"""

import time

from minros_tools import common as c

from minrospy import Node
from minrospy.interfaces.geometry_msgs import Vector3

CH_SEND = 0  # cihaz unreliable sub
CH_RECV = 1  # cihaz unreliable pub (echo)

ROUNDS = 100000
TIMEOUT = 2.0  # saniye / tur


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
        ok = c.approx(msg.x, x * 2) and c.approx(msg.y, y * 2) and c.approx(msg.z, z * 2)
        rtt_ms = (t1 - t0) * 1000.0
        latencies.append(rtt_ms)

        status = f"{c.GREEN}OK{c.RESET}" if ok else f"{c.RED}VERİ HATASI{c.RESET}"
        print(f"  tur {i + 1:3d}/{ROUNDS}  {rtt_ms:6.2f} ms  {status}")

    c.print_latency_report(ROUNDS, latencies, errors)
    ser.close()


if __name__ == "__main__":
    main()
