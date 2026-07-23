"""tools/ scriptleri için ortak yardımcılar.

pyserial transportu, spin döngüleri, renkli çıktı, test vektörü gibi tekrar
eden parçaları tek yerde toplar. Böylece her araç wire protokolünü elle
yeniden yazmaz; doğrudan minrospy kullanır.

Bağımlılıklar pip'ten gelir (bkz. tools/requirements.txt):
    pip install -r tools/requirements.txt
"""

import cmath
import random
import sys
import time

import serial

from minrospy import Transport

# ── ANSI renkleri ────────────────────────────────────────────────────────────
BOLD = "\033[1m"
GREEN = "\033[32m"
RED = "\033[31m"
CYAN = "\033[36m"
YELLOW = "\033[33m"
RESET = "\033[0m"


# ── Argüman & seri port ──────────────────────────────────────────────────────
def parse_args(default_port: str = "/dev/ttyACM0", default_baud: int = 115200):
    port = sys.argv[1] if len(sys.argv) > 1 else default_port
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else default_baud
    return port, baud


def open_serial(port: str, baud: int, timeout: float = 0.05) -> serial.Serial:
    try:
        return serial.Serial(port, baud, timeout=timeout)
    except serial.SerialException as e:
        print(f"{RED}Port açılamadı: {e}{RESET}")
        sys.exit(1)


def make_transport(ser: serial.Serial) -> Transport:
    """pyserial üzerinden minrospy Transport'u kurar."""
    return Transport(
        send_bytes=ser.write,
        read_bytes=ser.read,
        get_size=lambda: ser.in_waiting,
        get_time=lambda: int(time.monotonic() * 1000),
    )


# ── Spin yardımcıları (RawNode/Node fark etmez: ikisi de spin_once() sunar) ────
#
# Varsayılan olarak UYKUSUZ (busy-poll): gecikme ölçümünde doğruluk için. USB-CDC
# hattında RTT sub-milisaniye olabildiğinden araya time.sleep koymak ölçümü
# şişirir (tek bir 0.5 ms uyku, 0.27 ms'lik RTT'yi ikiye katlar). Eski testlerin
# bloklayan ser.read(1) döngüsü de fiilen böyle davranıyordu.
# Zamanlama kritik olmayan çağrılar idle_sleep ile CPU'yu rahatlatabilir.
def spin_until(node, predicate, timeout_s: float, idle_sleep: float = 0.0) -> bool:
    """predicate() True olana ya da timeout dolana kadar spin'ler."""
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        node.spin_once()
        if predicate():
            return True
        if idle_sleep:
            time.sleep(idle_sleep)
    return False


def spin_for(node, duration_s: float, idle_sleep: float = 0.0) -> None:
    """duration_s boyunca spin'ler (gelen tüm frame'leri işler)."""
    deadline = time.monotonic() + duration_s
    while time.monotonic() < deadline:
        node.spin_once()
        if idle_sleep:
            time.sleep(idle_sleep)


# ── Test vektörleri (karmaşık sayı çiftleri → Vector3) ───────────────────────
def complex_vectors(n: int, seed: int = 42) -> list[tuple[float, float, float]]:
    """Her tur için (Re(c1), Im(c1), Re(c2)) üçlüsü üretir."""
    rng = random.Random(seed)
    out = []
    for _ in range(n):
        r1, r2 = rng.uniform(0.5, 100.0), rng.uniform(0.5, 100.0)
        t1, t2 = rng.uniform(0, 2 * 3.14159265), rng.uniform(0, 2 * 3.14159265)
        c1, c2 = cmath.rect(r1, t1), cmath.rect(r2, t2)
        out.append((c1.real, c1.imag, c2.real))
    return out
