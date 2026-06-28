#!/usr/bin/env python3
"""
minros best-effort throughput testi — kontrollü hız, eş zamanlı echo sayımı.

Kullanım:
    python3 minros_throughput.py [PORT] [BAUD] [MESAJ_SAYISI] [HEDEF_RATE_MSG_S]

Varsayılan: /dev/ttyACM0 115200 1000 500
"""

import serial
import struct
import sys
import time
import random
import threading

HEADER  = b'\x6D\x72\x6F\x73'
CH_SEND = 0x02
CH_RECV = 0x03

BOLD  = "\033[1m"
GREEN = "\033[32m"
RED   = "\033[31m"
CYAN  = "\033[36m"
RESET = "\033[0m"


def crc8(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc


def build_frame(ch_id: int, payload: bytes, seq: int = 0) -> bytes:
    data = bytes([ch_id, seq]) + payload
    return HEADER + bytes([len(data)]) + data + bytes([crc8(data)])


def pack_v3(x: float, y: float, z: float) -> bytes:
    return struct.pack('<fff', x, y, z)


def echo_reader(ser: serial.Serial, stop_event: threading.Event, counter: list):
    """Gönderimle eş zamanlı çalışır, gelen CH_RECV frame'lerini sayar."""
    state  = 0
    hpos   = 0
    length = 0
    buf    = bytearray()

    while not stop_event.is_set():
        try:
            waiting = ser.in_waiting
            chunk = ser.read(waiting if waiting else 1)
        except serial.SerialException:
            break
        if not chunk:
            continue

        for v in chunk:
            if state == 0:
                if v == HEADER[hpos]:
                    hpos += 1
                    if hpos == 4:
                        state, hpos = 1, 0
                else:
                    hpos = 1 if v == HEADER[0] else 0

            elif state == 1:
                if v < 3 or v > 249:
                    state = hpos = 0
                    continue
                length, buf, state = v, bytearray(), 2

            elif state == 2:
                buf.append(v)
                if len(buf) == length:
                    state = 3

            elif state == 3:
                state = hpos = 0
                if v != crc8(bytes(buf)):
                    continue
                if buf[0] == CH_RECV:
                    counter[0] += 1


def main():
    port        = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
    baud        = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
    n_msgs      = int(sys.argv[3]) if len(sys.argv) > 3 else 1000
    target_rate = int(sys.argv[4]) if len(sys.argv) > 4 else 500  # msg/s

    interval = 1.0 / target_rate

    try:
        ser = serial.Serial(port, baud, timeout=0.1)
    except serial.SerialException as e:
        print(f"Port açılamadı: {e}")
        sys.exit(1)

    print(f"{BOLD}minros best-effort throughput testi — {port} @ {baud}{RESET}")
    print(f"  {n_msgs} mesaj, hedef hız: {target_rate} msg/s ({interval * 1000:.1f} ms aralık)\n")
    time.sleep(0.1)

    rng = random.Random(42)
    frames = []
    for i in range(n_msgs):
        x = rng.uniform(-100.0, 100.0)
        y = rng.uniform(-100.0, 100.0)
        z = rng.uniform(-100.0, 100.0)
        frames.append(build_frame(CH_SEND, pack_v3(x, y, z), seq=(i % 0xFE) + 1))

    ser.reset_input_buffer()

    received  = [0]
    stop_flag = threading.Event()
    reader    = threading.Thread(target=echo_reader, args=(ser, stop_flag, received), daemon=True)
    reader.start()

    # Kontrollü gönderim
    print("Gönderiliyor...")
    t0 = time.monotonic()
    next_send = t0
    for frame in frames:
        now = time.monotonic()
        if now < next_send:
            time.sleep(next_send - now)
        ser.write(frame)
        next_send += interval
    t1 = time.monotonic()

    send_duration = t1 - t0
    actual_rate   = n_msgs / send_duration

    print(f"  Gönderim süresi : {send_duration * 1000:.1f} ms")
    print(f"  Gerçek hız      : {actual_rate:.0f} msg/s\n")

    # Son echo'ların gelmesi için bekle
    drain_timeout = max(send_duration * 0.5, 0.5)
    print(f"Son echo'lar bekleniyor ({drain_timeout * 1000:.0f} ms)...")
    time.sleep(drain_timeout)
    stop_flag.set()
    reader.join(timeout=1.0)

    loss     = n_msgs - received[0]
    loss_pct = (loss / n_msgs) * 100

    print()
    print(f"{BOLD}Sonuçlar:{RESET}")
    print(f"  {CYAN}Throughput       : {actual_rate:.0f} msg/s{RESET}")
    print(f"  Gönderilen       : {n_msgs}")
    print(f"  Alınan (echo)    : {received[0]}")

    if loss == 0:
        print(f"  {GREEN}Kayıp oranı      : %0 (0/{n_msgs}){RESET}")
    else:
        print(f"  {RED}Kayıp oranı      : %{loss_pct:.1f} ({loss}/{n_msgs}){RESET}")

    pass_fail = f"{GREEN}Geçti{RESET}" if actual_rate >= 200 and loss_pct < 1.0 else f"{RED}Kaldı{RESET}"
    print(f"\n  Genel sonuç      : {pass_fail}")

    ser.close()


if __name__ == "__main__":
    main()
