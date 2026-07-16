"""minrospy ↔ minros wire-protokolü conformance testi.

Bu test, minrospy'yi `conformance/vectors.json` içindeki TARAFSIZ altın
vektörlere karşı sınar. Aynı vektörlere C++ tarafı da (conformance/cpp) sınanır;
böylece iki implementasyon birbirinden kayarsa testlerden biri kırmızıya döner.

Vektörler protokol spec'inden bağımsız üretilir (bkz. conformance/generate.py),
minrospy ya da minros'tan türetilmez.

Çalıştırma:
    python -m pytest lib/minrospy/tests/test_conformance.py
"""

import json
import os
import sys
from pathlib import Path

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from minrospy.core import wireframe
from minrospy.core.framer import Framer
from minrospy.interfaces.geometry_msgs import Quaternion, Twist, Vector3
from minrospy.interfaces.std_msgs import (
    Bool,
    Float32,
    Int8,
    Int16,
    Int32,
    PidGains,
    UInt8,
    UInt16,
    UInt32,
)

# conformance/vectors.json — repo kökünden çözülür (lib/minrospy/tests/../../../)
VECTORS_PATH = Path(__file__).resolve().parents[3] / "conformance" / "vectors.json"

CLASSES = {
    "Float32": Float32, "Int32": Int32, "Int16": Int16, "Int8": Int8,
    "UInt32": UInt32, "UInt16": UInt16, "UInt8": UInt8, "Bool": Bool,
    "Vector3": Vector3, "Quaternion": Quaternion, "Twist": Twist, "PidGains": PidGains,
}


def _load():
    if not VECTORS_PATH.exists():
        pytest.skip(f"vektör dosyası yok: {VECTORS_PATH} (önce conformance/generate.py çalıştır)")
    return json.loads(VECTORS_PATH.read_text())


VECTORS = _load()


def _build_message(mtype: str, fields):
    """Vektör alan listesinden ('linear.x', ctype, value) bir mesaj örneği kurar."""
    msg = CLASSES[mtype]()
    for path, _ctype, value in fields:
        obj = msg
        *parents, attr = path.split(".")
        for p in parents:
            obj = getattr(obj, p)
        setattr(obj, attr, value)
    return msg


# ── CRC-8/SMBUS ──────────────────────────────────────────────────────────────
@pytest.mark.parametrize("vec", VECTORS["crc8"], ids=lambda v: v["name"])
def test_crc8(vec):
    data = bytes.fromhex(vec["data"])
    assert wireframe.crc8(data) == vec["crc"]


# ── std_msgs serileştirme ────────────────────────────────────────────────────
@pytest.mark.parametrize("vec", VECTORS["messages"], ids=lambda v: v["name"])
def test_message_serialize(vec):
    msg = _build_message(vec["type"], vec["fields"])
    assert msg.to_bytes().hex() == vec["bytes"]


@pytest.mark.parametrize("vec", VECTORS["messages"], ids=lambda v: v["name"])
def test_message_roundtrip(vec):
    expected = bytes.fromhex(vec["bytes"])
    restored = CLASSES[vec["type"]].from_bytes(expected)
    assert restored is not None
    assert restored.to_bytes() == expected


# ── Tam wire frame (Framer) ──────────────────────────────────────────────────
@pytest.mark.parametrize("vec", VECTORS["frames"], ids=lambda v: v["name"])
def test_frame_build(vec):
    # Core SEQ bilmez: seq, framer'a opak 1-baytlık HEAD öneki olarak verilir.
    framer = Framer()
    frame = framer.build(
        vec["ch_id"], bytes.fromhex(vec["payload"]), head=bytes([vec["seq"]])
    )
    assert frame is not None
    assert frame.hex() == vec["frame"]


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v"]))
