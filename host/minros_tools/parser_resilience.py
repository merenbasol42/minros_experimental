#!/usr/bin/env python3
"""
minros parser dayanıklılığı testi — GERÇEK KART üzerinden.

host/test/test_parser_resilience.py ve firmware/test/test_parser_resilience/
aynı senaryoları bellek içi byte akışıyla sınıyor (donanımsız). Bu araç
aynı senaryoları gerçek hatta sınar: bozuk/yarım/örtüşen baytlar doğrudan
seri porta yazılır, hemen ardından geçerli bir Vector3 gönderilir. Kartın
firmware Parser'ı (bkz. commit 43ffb80: resync/gürültü-kilitlenmesi
düzeltmesi) gürültüden toparlanıp geçerli mesajı doğru echo'larsa senaryo
geçer.

Ham RawNode kullanılır: gürültü `ser.write()` ile doğrudan hatta yazılır,
ardından RawNode.publish ile çerçevelenmiş geçerli mesaj gönderilir — ikisi
aynı seri akışta ardışık baytlar olarak birleşir (gerçek bir hat
kopması/gürültüsü sonrası geçerli mesaj gelmesiyle birebir aynı durum).

Kullanım:
    minros-parser-resilience [PORT] [BAUD]
"""

import random
import time

from minros_tools import common as c

from minrospy import RawNode
from minrospy.core import wireframe
from minrospy.core.framer import Framer
from minrospy.interfaces.geometry_msgs import Vector3

CH_SEND = 0  # cihaz unreliable sub
CH_RECV = 1  # cihaz unreliable pub (echo)

GAIN = 2.0  # main.cpp setup()'taki varsayılan PARAM_GAIN (echo = girdi * gain)

PASS = f"{c.GREEN}GEÇTİ{c.RESET}"
FAIL = f"{c.RED}BAŞARISIZ{c.RESET}"

NOISE_SIZE = 1000  # test senaryolarında gönderilen gürültü baytlarının boyutu


def build_noise_scenarios() -> list[tuple[str, bytes]]:
    """host/test/test_parser_resilience.py ile aynı gürültü desenleri."""
    framer = Framer()
    rng = random.Random(42)

    random_noise = bytes(
        b for b in (rng.randrange(256) for _ in range(2000)) if b not in wireframe.HEADER
    )[:NOISE_SIZE]

    invalid_length = wireframe.HEADER + bytes([0])  # 0 < MIN_DATA_LEN

    crc_mismatch = bytearray(framer.build(250, b"corrupt"))
    crc_mismatch[-1] ^= 0xFF  # yalnızca CRC baytını boz

    overlapping_header = b"mr"  # HEADER'ın kendi baytlarıyla çakışan yarım eşleşme

    return [
        (f"rastgele gürültü öneki ({NOISE_SIZE} bayt)", random_noise),
        ("geçersiz LEN baytı", invalid_length),
        ("CRC uyuşmazlığı (tam frame, bozuk CRC)", bytes(crc_mismatch)),
        ("örtüşen header öneki ('mr')", overlapping_header),
    ]


def run_scenario(node: RawNode, ser, received: list, name: str, noise: bytes, seed: float) -> bool:
    """`noise` baytlarını hatta yazar, ardından geçerli bir Vector3 gönderir;
    kart resync olup echo'yu doğru üretirse True döner."""
    ser.reset_input_buffer()
    received.clear()

    x, y, z = seed, seed + 1.0, seed + 2.0
    if noise:
        ser.write(noise)
        ser.flush()
    node.publish(CH_SEND, Vector3(x, y, z).to_bytes())

    c.spin_until(node, lambda: bool(received), timeout_s=1.0)

    msg = Vector3.from_bytes(received[0]) if received else None
    expected = (x * GAIN, y * GAIN, z * GAIN)
    got = (msg.x, msg.y, msg.z) if msg is not None else None

    if got is not None and all(c.approx(a, b) for a, b in zip(got, expected)):
        print(f"  {PASS} — {name}")
        return True

    print(f"  {FAIL} — {name} (beklenen echo alınamadı; alınan: {got})")
    return False


def main():
    port, baud = c.parse_args()
    ser = c.open_serial(port, baud, timeout=0.05)

    node = RawNode()
    node.transport = c.make_transport(ser)

    received: list[bytes] = []
    node.subscribe(CH_RECV, received.append)

    print(f"{c.BOLD}minros parser dayanıklılığı testi — {port} @ {baud}{c.RESET}\n")
    time.sleep(0.1)

    results = []
    for i, (name, noise) in enumerate(build_noise_scenarios()):
        seed = 10.0 * (i + 1)
        results.append(run_scenario(node, ser, received, name, noise, seed))

    passed = sum(results)
    total = len(results)
    print(f"\n{c.BOLD}Sonuç: {passed}/{total} test geçti{c.RESET}")

    ser.close()


if __name__ == "__main__":
    main()
