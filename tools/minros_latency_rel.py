#!/usr/bin/env python3
"""
minros reliable latency test — ch 0x04 → ch 0x05, karmaşık sayılarla 10000 tur.

Kullanım:
    python3 minros_latency_rel.py [PORT] [BAUD]

Varsayılan: /dev/ttyACM0 115200
"""

import serial
import struct
import sys
import time
import cmath
import random

# ── Protokol sabitleri ───────────────────────────────────────────────────────

HEADER      = b'\x6D\x72\x6F\x73'
CH_SEND     = 0x04
CH_RECV     = 0x05
ACK_CH      = 249          # 0xF9
ACK_TYPE    = 0x06


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


def ack_frame(ch_id: int, seq: int) -> bytes:
    return build_frame(ACK_CH, bytes([ACK_TYPE, ch_id, seq]))


# ── Senkron okuyucu — hedef kanala ulaşana kadar ACK'ları atla ──────────────

def read_target_frame(ser: serial.Serial, target_ch: int, timeout_s: float):
    """
    target_ch'a sahip ilk geçerli frame'i döndürür.
    ACK frame'leri (ch=249) sessizce atlanır.
    None → timeout.
    """
    deadline = time.monotonic() + timeout_s
    state = 0
    hpos = length = 0
    pending = bytearray()

    def _read_until(n: int) -> bool:
        """pending'e en az n byte gelene kadar döner; deadline aşılırsa False."""
        while len(pending) < n:
            rem = deadline - time.monotonic()
            if rem <= 0:
                return False
            ser.timeout = min(rem, 0.05)
            chunk = ser.read(max(1, ser.in_waiting or n - len(pending)))
            if chunk:
                pending.extend(chunk)
        return True

    while time.monotonic() < deadline:
        if not _read_until(1):
            break
        v = pending[0]
        del pending[0]

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
            length = v
            # length byte'ları tek seferde oku
            if not _read_until(length + 1):  # +1 CRC
                break
            data = bytes(pending[:length])
            crc  = pending[length]
            del pending[:length + 1]
            state = hpos = 0
            if crc != crc8(data):
                continue
            ch_id = data[0]
            seq   = data[1]
            if ch_id == ACK_CH:
                continue
            if ch_id != target_ch:
                continue
            return ch_id, seq, bytes(data[2:])

    return None


# ── Test vektörleri ──────────────────────────────────────────────────────────

def make_test_vectors(n: int) -> list[tuple[float, float, float]]:
    rng = random.Random(42)
    vectors = []
    for _ in range(n):
        r1, r2 = rng.uniform(0.5, 100.0), rng.uniform(0.5, 100.0)
        t1, t2 = rng.uniform(0, 2 * 3.14159265), rng.uniform(0, 2 * 3.14159265)
        c1 = cmath.rect(r1, t1)
        c2 = cmath.rect(r2, t2)
        vectors.append((c1.real, c1.imag, c2.real))
    return vectors


# ── Ana ──────────────────────────────────────────────────────────────────────

ROUNDS  = 100000
TIMEOUT = 2.0

BOLD  = "\033[1m"
GREEN = "\033[32m"
RED   = "\033[31m"
CYAN  = "\033[36m"
RESET = "\033[0m"


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 9600

    try:
        ser = serial.Serial(port, baud, timeout=0.05)
    except serial.SerialException as e:
        print(f"Port açılamadı: {e}")
        sys.exit(1)

    print(f"{BOLD}minros reliable latency test — {port} @ {baud} — {ROUNDS} tur{RESET}\n")
    time.sleep(0.1)

    vectors   = make_test_vectors(ROUNDS)
    latencies = []
    errors    = 0
    send_seq  = 0

    for i, (x, y, z) in enumerate(vectors):
        # seq 0xFF başlangıç sentinel'i olduğundan kaçın
        send_seq = (send_seq % 0xFE) + 1

        frame = build_frame(CH_SEND, pack_v3(x, y, z), send_seq)

        ser.reset_input_buffer()
        t0 = time.monotonic()
        ser.write(frame)
        result = read_target_frame(ser, CH_RECV, TIMEOUT)
        t1 = time.monotonic()

        if result is None:
            print(f"  tur {i+1:5d}/{ROUNDS}  {RED}TIMEOUT{RESET}")
            errors += 1
            continue

        ch_id, recv_seq, payload = result

        # Arduino reliable publisher ACK bekliyor — hemen gönder
        ser.write(ack_frame(CH_RECV, recv_seq))

        if len(payload) < 12:
            print(f"  tur {i+1:5d}/{ROUNDS}  {RED}KISA PAYLOAD{RESET}")
            errors += 1
            continue

        ok = payload[:12] == pack_v3(x * 2, y * 2, z * 2)
        rtt_ms = (t1 - t0) * 1000.0
        latencies.append(rtt_ms)

        status = f"{GREEN}OK{RESET}" if ok else f"{RED}VERİ HATASI{RESET}"
        print(f"  tur {i+1:5d}/{ROUNDS}  {rtt_ms:6.2f} ms  {status}")

    print()
    if latencies:
        avg = sum(latencies) / len(latencies)
        mn  = min(latencies)
        mx  = max(latencies)
        print(f"{BOLD}Sonuçlar ({len(latencies)}/{ROUNDS} başarılı):{RESET}")
        print(f"  {CYAN}Ortalama : {avg:.2f} ms{RESET}")
        print(f"  Min      : {mn:.2f} ms")
        print(f"  Max      : {mx:.2f} ms")
        if errors:
            print(f"  {RED}Kayıp    : {errors}{RESET}")
    else:
        print(f"{RED}Hiçbir yanıt alınamadı.{RESET}")

    ser.close()


if __name__ == "__main__":
    main()
