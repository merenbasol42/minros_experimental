#!/usr/bin/env python3
"""
minros latency test — karmaşık sayılarla 100 tur, ortalama RTT ölçer.

Kullanım:
    python3 minros_latency.py [PORT] [BAUD]

Varsayılan: /dev/ttyACM0 115200
"""

import serial
import struct
import sys
import time
import cmath
import random

# ── Protokol (minros_serial.py ile aynı) ────────────────────────────────────

HEADER  = b'\x6D\x72\x6F\x73'
CH_SEND = 0x02
CH_RECV = 0x03


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


def unpack_v3(payload: bytes) -> tuple:
    return struct.unpack('<fff', payload[:12])


# ── Senkron tek-tur okuyucu ──────────────────────────────────────────────────

def read_frame(ser: serial.Serial, timeout_s: float) -> tuple | None:
    """Tek bir geçerli frame gelene kadar okur. None → timeout/hata."""
    deadline = time.monotonic() + timeout_s
    state    = 0   # 0=header, 1=len, 2=data, 3=crc
    hpos     = 0
    length   = 0
    buf      = bytearray()

    while time.monotonic() < deadline:
        b = ser.read(1)
        if not b:
            continue
        v = b[0]

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
            if v == crc8(bytes(buf)):
                return buf[0], buf[1], bytes(buf[2:])   # ch_id, seq, payload
            state = hpos = 0   # CRC hatası, devam

    return None


# ── Test vektörleri: karmaşık sayı çiftleri ─────────────────────────────────

def make_test_vectors(n: int) -> list[tuple[float, float, float]]:
    """
    Her tur için farklı bir karmaşık sayı çifti üretir:
      c1 = r1 * e^(i*theta1),  c2 = r2 * e^(i*theta2)
    Bunları (Re(c1), Im(c1), Re(c2)) olarak Vector3'e paketler.
    Im(c2) ölçüme gerek olmadığından z eksenine sadece Re(c2) koyulur.
    """
    rng = random.Random(42)
    vectors = []
    for _ in range(n):
        r1, r2   = rng.uniform(0.5, 100.0), rng.uniform(0.5, 100.0)
        t1, t2   = rng.uniform(0, 2 * 3.14159265), rng.uniform(0, 2 * 3.14159265)
        c1       = cmath.rect(r1, t1)
        c2       = cmath.rect(r2, t2)
        vectors.append((c1.real, c1.imag, c2.real))
    return vectors


# ── Ana ─────────────────────────────────────────────────────────────────────

ROUNDS   = 100
TIMEOUT  = 2.0   # saniye / tur

BOLD  = "\033[1m"
GREEN = "\033[32m"
RED   = "\033[31m"
CYAN  = "\033[36m"
RESET = "\033[0m"


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200

    try:
        ser = serial.Serial(port, baud, timeout=0.05)
    except serial.SerialException as e:
        print(f"Port açılamadı: {e}")
        sys.exit(1)

    print(f"{BOLD}minros latency test — {port} @ {baud} — {ROUNDS} tur{RESET}\n")
    time.sleep(0.1)   # Arduino reset süresi

    vectors = make_test_vectors(ROUNDS)
    latencies = []
    errors    = 0

    for i, (x, y, z) in enumerate(vectors):
        frame = build_frame(CH_SEND, pack_v3(x, y, z))

        ser.reset_input_buffer()
        t0 = time.monotonic()
        ser.write(frame)
        result = read_frame(ser, TIMEOUT)
        t1 = time.monotonic()

        if result is None:
            print(f"  tur {i+1:3d}/{ROUNDS}  {RED}TIMEOUT{RESET}")
            errors += 1
            continue

        ch_id, _, payload = result
        if ch_id != CH_RECV or len(payload) < 12:
            print(f"  tur {i+1:3d}/{ROUNDS}  {RED}YANLIŞ KANAL veya BOYUT (ch=0x{ch_id:02X}){RESET}")
            errors += 1
            continue

        ok = payload[:12] == pack_v3(x * 2, y * 2, z * 2)
        rtt_ms = (t1 - t0) * 1000.0
        latencies.append(rtt_ms)

        status = f"{GREEN}OK{RESET}" if ok else f"{RED}VERI HATASI{RESET}"
        print(f"  tur {i+1:3d}/{ROUNDS}  {rtt_ms:6.2f} ms  {status}")

    print()
    if latencies:
        avg  = sum(latencies) / len(latencies)
        mn   = min(latencies)
        mx   = max(latencies)
        lost = errors
        print(f"{BOLD}Sonuçlar ({len(latencies)}/{ROUNDS} başarılı):{RESET}")
        print(f"  {CYAN}Ortalama : {avg:.2f} ms{RESET}")
        print(f"  Min      : {mn:.2f} ms")
        print(f"  Max      : {mx:.2f} ms")
        if lost:
            print(f"  {RED}Kayıp    : {lost}{RESET}")
    else:
        print(f"{RED}Hiçbir yanıt alınamadı.{RESET}")

    ser.close()


if __name__ == "__main__":
    main()
