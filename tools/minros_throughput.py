#!/usr/bin/env python3
"""
minros best-effort throughput testi — kontrollü hız, eş zamanlı echo sayımı.

CH_SEND'e Vector3 akıtır, CH_RECV echo'larını arka planda sayar. Protokol
minrospy ile yönetilir.

Kullanım:
    python3 minros_throughput.py [PORT] [BAUD] [MESAJ_SAYISI] [HEDEF_RATE_MSG_S]

Varsayılan: /dev/ttyACM0 115200 1000 500
"""

import os
import random
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as c

from minrospy import Node
from minrospy.interfaces.geometry_msgs import Vector3

CH_SEND = 0  # cihaz unreliable sub
CH_RECV = 1  # cihaz unreliable pub (echo)


def main():
    port, baud = c.parse_args()
    n_msgs = int(sys.argv[3]) if len(sys.argv) > 3 else 1000
    target_rate = int(sys.argv[4]) if len(sys.argv) > 4 else 500  # msg/s
    interval = 1.0 / target_rate

    ser = c.open_serial(port, baud, timeout=0.1)

    node = Node()
    node.transport = c.make_transport(ser)

    received = [0]

    def on_echo(_msg: Vector3):
        received[0] += 1

    node.create_subscription(Vector3, CH_RECV, on_echo)
    pub = node.create_publisher(Vector3, CH_SEND)

    print(f"{c.BOLD}minros best-effort throughput testi — {port} @ {baud}{c.RESET}")
    print(f"  {n_msgs} mesaj, hedef hız: {target_rate} msg/s ({interval * 1000:.1f} ms aralık)\n")
    time.sleep(0.1)

    rng = random.Random(42)
    msgs = [
        Vector3(rng.uniform(-100, 100), rng.uniform(-100, 100), rng.uniform(-100, 100))
        for _ in range(n_msgs)
    ]

    ser.reset_input_buffer()

    stop = threading.Event()

    def reader():
        while not stop.is_set():
            node.spin_once()
            time.sleep(0.0005)

    t = threading.Thread(target=reader, daemon=True)
    t.start()

    print("Gönderiliyor...")
    t0 = time.monotonic()
    next_send = t0
    for msg in msgs:
        now = time.monotonic()
        if now < next_send:
            time.sleep(next_send - now)
        pub.publish(msg)
        next_send += interval
    t1 = time.monotonic()

    send_duration = t1 - t0
    actual_rate = n_msgs / send_duration
    print(f"  Gönderim süresi : {send_duration * 1000:.1f} ms")
    print(f"  Gerçek hız      : {actual_rate:.0f} msg/s\n")

    drain_timeout = max(send_duration * 0.5, 0.5)
    print(f"Son echo'lar bekleniyor ({drain_timeout * 1000:.0f} ms)...")
    time.sleep(drain_timeout)
    stop.set()
    t.join(timeout=1.0)

    loss = n_msgs - received[0]
    loss_pct = (loss / n_msgs) * 100

    print()
    print(f"{c.BOLD}Sonuçlar:{c.RESET}")
    print(f"  {c.CYAN}Throughput       : {actual_rate:.0f} msg/s{c.RESET}")
    print(f"  Gönderilen       : {n_msgs}")
    print(f"  Alınan (echo)    : {received[0]}")

    if loss == 0:
        print(f"  {c.GREEN}Kayıp oranı      : %0 (0/{n_msgs}){c.RESET}")
    else:
        print(f"  {c.RED}Kayıp oranı      : %{loss_pct:.1f} ({loss}/{n_msgs}){c.RESET}")

    pass_fail = (
        f"{c.GREEN}Geçti{c.RESET}"
        if actual_rate >= 200 and loss_pct < 1.0
        else f"{c.RED}Kaldı{c.RESET}"
    )
    print(f"\n  Genel sonuç      : {pass_fail}")

    ser.close()


if __name__ == "__main__":
    main()
