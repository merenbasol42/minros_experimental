#!/usr/bin/env python3
"""
minros serial monitor — Vector3 gönderir ve alır, terminalde gösterir.

Kullanım:
    python3 minros_serial.py [PORT] [BAUD]

Varsayılan: /dev/ttyUSB0 115200

Komutlar (çalışırken):
    x y z   → channel 0x00'a Vector3 gönder  (örn: 1.0 2.5 -3.0)
    q       → çık
"""

import serial
import struct
import threading
import sys
import time

# ── Wire sabitler ────────────────────────────────────────────────────────────

HEADER      = b'\x6D\x72\x6F\x73'   # "mros"
CH_SEND     = 0x00
CH_RECV     = 0x01


def crc8(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc


def build_frame(ch_id: int, payload: bytes, seq: int = 0) -> bytes:
    data = bytes([ch_id, seq]) + payload
    length = len(data)
    return HEADER + bytes([length]) + data + bytes([crc8(data)])


def pack_vector3(x: float, y: float, z: float) -> bytes:
    return struct.pack('<fff', x, y, z)


def unpack_vector3(payload: bytes) -> tuple[float, float, float]:
    return struct.unpack('<fff', payload[:12])


# ── Parser (durum makinesi) ──────────────────────────────────────────────────

class FrameParser:
    S_HEADER, S_LEN, S_DATA, S_CRC = range(4)

    def __init__(self, on_frame):
        self.on_frame = on_frame
        self._reset()

    def _reset(self):
        self.state  = self.S_HEADER
        self.hpos   = 0
        self.length = 0
        self.buf    = bytearray()

    def feed(self, data: bytes):
        for b in data:
            self._process(b)

    def _process(self, b: int):
        if self.state == self.S_HEADER:
            if b == HEADER[self.hpos]:
                self.hpos += 1
                if self.hpos == len(HEADER):
                    self.state = self.S_LEN
                    self.hpos  = 0
            else:
                self.hpos = 1 if b == HEADER[0] else 0

        elif self.state == self.S_LEN:
            if b < 3 or b > 249:   # min: CH_ID(1)+SEQ(1)+PAYLOAD(1)=3
                self._reset()
                return
            self.length = b
            self.buf    = bytearray()
            self.state  = self.S_DATA

        elif self.state == self.S_DATA:
            self.buf.append(b)
            if len(self.buf) == self.length:
                self.state = self.S_CRC

        elif self.state == self.S_CRC:
            expected = crc8(bytes(self.buf))
            if b == expected:
                ch_id   = self.buf[0]
                seq     = self.buf[1]
                payload = bytes(self.buf[2:])
                self.on_frame(ch_id, seq, payload)
            else:
                print(f"\033[33m[WARN] CRC hatası (beklenen=0x{expected:02X} gelen=0x{b:02X})\033[0m")
            self._reset()


# ── Renkli terminal çıktısı ──────────────────────────────────────────────────

CYAN  = "\033[36m"
GREEN = "\033[32m"
RESET = "\033[0m"
BOLD  = "\033[1m"


def fmt_v3(x, y, z) -> str:
    return f"x={x:+.4f}  y={y:+.4f}  z={z:+.4f}"


def on_frame_received(ch_id: int, seq: int, payload: bytes):
    ms = int(time.time() * 1000) % 1000
    ts = time.strftime("%H:%M:%S") + f".{ms:03d}"
    if ch_id == CH_RECV and len(payload) >= 12:
        x, y, z = unpack_vector3(payload)
        print(f"{CYAN}[{ts}] ← ch=0x{ch_id:02X} seq={seq:3d}  {fmt_v3(x,y,z)}{RESET}")
    else:
        hex_str = payload.hex(' ')
        print(f"{CYAN}[{ts}] ← ch=0x{ch_id:02X} seq={seq:3d}  [{hex_str}]{RESET}")


# ── Okuma thread'i ───────────────────────────────────────────────────────────

def reader_thread(ser: serial.Serial, stop_event: threading.Event):
    parser = FrameParser(on_frame_received)
    while not stop_event.is_set():
        try:
            waiting = ser.in_waiting
            if waiting:
                parser.feed(ser.read(waiting))
            else:
                time.sleep(0.005)
        except serial.SerialException:
            break


# ── Ana döngü ────────────────────────────────────────────────────────────────

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200

    try:
        ser = serial.Serial(port, baud, timeout=0.1)
    except serial.SerialException as e:
        print(f"Port açılamadı: {e}")
        sys.exit(1)

    print(f"{BOLD}minros serial monitor — {port} @ {baud}{RESET}")
    print("Gönder: <x> <y> <z>   Çık: q\n")

    stop = threading.Event()
    t = threading.Thread(target=reader_thread, args=(ser, stop), daemon=True)
    t.start()

    try:
        while True:
            line = input()
            if line.strip().lower() == 'q':
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

            payload = pack_vector3(x, y, z)
            frame   = build_frame(CH_SEND, payload)
            ser.write(frame)
            ms = int(time.time() * 1000) % 1000
            ts = time.strftime("%H:%M:%S") + f".{ms:03d}"
            print(f"{GREEN}[{ts}] → ch=0x{CH_SEND:02X} seq=  0  {fmt_v3(x,y,z)}{RESET}")

    except (EOFError, KeyboardInterrupt):
        pass
    finally:
        stop.set()
        ser.close()
        print("\nKapatıldı.")


if __name__ == "__main__":
    main()
