#!/usr/bin/env python3
"""
minros serial monitor — Vector3 gönderir ve alır, terminalde gösterir.

Protokol minrospy ile yönetilir (elle frame kurma/parse yok).

Kullanım:
    python3 minros_serial.py [PORT] [BAUD]

Varsayılan: /dev/ttyACM0 115200

Komutlar (çalışırken):
    x y z   → CH_SEND'e Vector3 gönder  (örn: 1.0 2.5 -3.0)
    q       → çık
"""

import os
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as c

from minrospy import Node
from minrospy.interfaces.geometry_msgs import Vector3

CH_SEND = 0x00
CH_RECV = 0x01


def fmt_v3(v: Vector3) -> str:
    return f"x={v.x:+.4f}  y={v.y:+.4f}  z={v.z:+.4f}"


def main():
    port, baud = c.parse_args()
    ser = c.open_serial(port, baud, timeout=0.05)

    node = Node()
    node.transport = c.make_transport(ser)

    def on_recv(msg: Vector3):
        ms = int(time.time() * 1000) % 1000
        ts = time.strftime("%H:%M:%S") + f".{ms:03d}"
        print(f"{c.CYAN}[{ts}] ← ch=0x{CH_RECV:02X}  {fmt_v3(msg)}{c.RESET}")

    node.create_subscription(Vector3, CH_RECV, on_recv)
    pub = node.create_publisher(Vector3, CH_SEND)

    print(f"{c.BOLD}minros serial monitor — {port} @ {baud}{c.RESET}")
    print("Gönder: <x> <y> <z>   Çık: q\n")

    stop = threading.Event()

    def reader():
        while not stop.is_set():
            node.spin_once()
            time.sleep(0.005)

    t = threading.Thread(target=reader, daemon=True)
    t.start()

    try:
        while True:
            line = input()
            if line.strip().lower() == "q":
                break
            parts = line.strip().split()
            if len(parts) != 3:
                print("Hatalı format. Örnek: 1.0 0.0 -2.5")
                continue
            try:
                x, y, z = float(parts[0]), float(parts[1]), float(parts[2])
            except ValueError:
                print("Sayısal değer giriniz.")
                continue

            pub.publish(Vector3(x, y, z))
            ms = int(time.time() * 1000) % 1000
            ts = time.strftime("%H:%M:%S") + f".{ms:03d}"
            print(f"{c.GREEN}[{ts}] → ch=0x{CH_SEND:02X}  {fmt_v3(Vector3(x, y, z))}{c.RESET}")

    except (EOFError, KeyboardInterrupt):
        pass
    finally:
        stop.set()
        t.join(timeout=1.0)
        ser.close()
        print("\nKapatıldı.")


if __name__ == "__main__":
    main()
