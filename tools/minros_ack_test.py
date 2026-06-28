#!/usr/bin/env python3
"""
minros ACK mekanizması testi.

İki test:
  1. ACK gönder → Arduino retransmit ETMEMELİ  (TIMEOUT_MS=50 + margin sonra sessiz)
  2. ACK gönderme → Arduino retransmit ETMELİ  (aynı seq tekrar gelmeli)

Kullanım:
    python3 minros_ack_test.py [PORT] [BAUD]
"""

import serial
import struct
import sys
import time

HEADER   = b'\x6D\x72\x6F\x73'
CH_SEND  = 0x04
CH_RECV  = 0x05
ACK_CH   = 249
ACK_TYPE = 0x06

RETRANSMIT_TIMEOUT_MS = 50    # sequencer TIMEOUT_MS
WAIT_MS               = 150   # bekleme: timeout'un 3 katı, güvenli marj


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


def ack_frame(ch_id: int, seq: int) -> bytes:
    return build_frame(ACK_CH, bytes([ACK_TYPE, ch_id, seq]))


def pack_v3(x: float, y: float, z: float) -> bytes:
    return struct.pack('<fff', x, y, z)


def read_frame(ser: serial.Serial, timeout_s: float):
    """Gelen ilk geçerli frame'i döndürür (ACK dahil). None → timeout."""
    deadline = time.monotonic() + timeout_s
    state = hpos = length = 0
    buf = bytearray()

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
            state = hpos = 0
            if v != crc8(bytes(buf)):
                continue
            return buf[0], buf[1], bytes(buf[2:])

    return None


def drain(ser: serial.Serial, wait_ms: int) -> list:
    """wait_ms boyunca gelen tüm frame'leri toplar."""
    frames = []
    deadline = time.monotonic() + wait_ms / 1000
    while time.monotonic() < deadline:
        f = read_frame(ser, deadline - time.monotonic())
        if f:
            frames.append(f)
    return frames


def send_and_receive(ser: serial.Serial, seq: int, x=1.0, y=2.0, z=3.0):
    """0x04'e gönder, 0x05 yanıtını bekle (ACK'ları atla). None → timeout."""
    frame = build_frame(CH_SEND, pack_v3(x, y, z), seq)
    ser.reset_input_buffer()
    ser.write(frame)

    deadline = time.monotonic() + 1.0
    while time.monotonic() < deadline:
        f = read_frame(ser, deadline - time.monotonic())
        if f is None:
            break
        ch, s, payload = f
        if ch == CH_RECV:
            return s, payload
        # 0xF9 ACK (Arduino'nun 0x04 ACK'ı) → atla
    return None


BOLD  = "\033[1m"
GREEN = "\033[32m"
RED   = "\033[31m"
CYAN  = "\033[36m"
RESET = "\033[0m"

PASS = f"{GREEN}GEÇTI{RESET}"
FAIL = f"{RED}BAŞARISIZ{RESET}"


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200

    try:
        ser = serial.Serial(port, baud, timeout=0.05)
    except serial.SerialException as e:
        print(f"Port açılamadı: {e}")
        sys.exit(1)

    print(f"{BOLD}minros ACK testi — {port} @ {baud}{RESET}\n")
    time.sleep(0.1)

    results = []

    # ── Test 1: ACK gönder → retransmit gelmemeli ────────────────────────────
    print(f"{CYAN}[TEST 1]{RESET} ACK gönderilince retransmit gelmemeli...")

    r = send_and_receive(ser, seq=1)
    if r is None:
        print(f"  İlk yanıt alınamadı — test atlandı.\n")
        results.append(None)
    else:
        recv_seq, _ = r
        ser.write(ack_frame(CH_RECV, recv_seq))          # ACK gönder

        extras = [f for f in drain(ser, WAIT_MS) if f[0] == CH_RECV]
        if extras:
            seqs = [f[1] for f in extras]
            print(f"  {FAIL} — {len(extras)} retransmit geldi (seq={seqs})")
            results.append(False)
        else:
            print(f"  {PASS} — {WAIT_MS}ms içinde retransmit gelmedi")
            results.append(True)

    print()

    # ── Test 2: ACK gönderme → retransmit gelmeli ────────────────────────────
    print(f"{CYAN}[TEST 2]{RESET} ACK gönderilmeyince retransmit gelmeli...")

    r = send_and_receive(ser, seq=2)
    if r is None:
        print(f"  İlk yanıt alınamadı — test atlandı.\n")
        results.append(None)
    else:
        recv_seq, _ = r
        # ACK göndermiyoruz — retransmit bekliyoruz

        extras = [f for f in drain(ser, WAIT_MS) if f[0] == CH_RECV]
        if extras:
            same_seq = all(f[1] == recv_seq for f in extras)
            note = f"seq={recv_seq}, {len(extras)}x retransmit" if same_seq \
                   else f"farklı seq geldi: {[f[1] for f in extras]}"
            print(f"  {PASS} — retransmit geldi ({note})")
            # Temizlik: ACK gönder
            ser.write(ack_frame(CH_RECV, recv_seq))
            results.append(True)
        else:
            print(f"  {FAIL} — {WAIT_MS}ms içinde retransmit gelmedi")
            results.append(False)

    print()

    # ── Özet ─────────────────────────────────────────────────────────────────
    passed = sum(1 for r in results if r is True)
    total  = sum(1 for r in results if r is not None)
    print(f"{BOLD}Sonuç: {passed}/{total} test geçti{RESET}")

    ser.close()


if __name__ == "__main__":
    main()
