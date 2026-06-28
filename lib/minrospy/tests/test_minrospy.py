"""minrospy birim testleri — wire uyumluluğu ve uçtan uca akış.

Çalıştırma:
    python -m pytest minrospy/tests
    veya
    python minrospy/tests/test_minrospy.py
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from minrospy import Node, NodeHL, Reliable, Transport
from minrospy.core import wireframe
from minrospy.core.framer import Framer
from minrospy.core.parser import Parser
from minrospy.std_msgs import Float32, Twist, Vector3


# ── Loopback transport ──────────────────────────────────────────────────────


class Loopback:
    """send_bytes ile yazılanı, read_bytes ile okutan basit FIFO transport."""

    def __init__(self):
        self.buf = bytearray()
        self._time = 0

    def send_bytes(self, data: bytes) -> None:
        self.buf.extend(data)

    def read_bytes(self, n: int) -> bytes:
        chunk = bytes(self.buf[:n])
        del self.buf[:n]
        return chunk

    def get_size(self) -> int:
        return len(self.buf)

    def get_time(self) -> int:
        return self._time

    def transport(self) -> Transport:
        return Transport(self.send_bytes, self.read_bytes, self.get_size, self.get_time)


# ── CRC / wire formatı ───────────────────────────────────────────────────────


def test_crc8_smbus_known_vector():
    # CRC-8/SMBUS("123456789") == 0xF4
    assert wireframe.crc8(b"123456789") == 0xF4


def test_framer_layout():
    # Core SEQ bilmez: DATA = CH_ID + PAYLOAD (head'siz).
    framer = Framer()
    frame = framer.build(ch_id=7, payload=b"\x01\x02")
    assert frame[:4] == wireframe.HEADER
    assert frame[4] == 3  # LEN = ch_id + 2 payload
    assert frame[5] == 7  # CH_ID
    assert frame[6:8] == b"\x01\x02"
    assert frame[8] == wireframe.crc8(bytes((7, 1, 2)))


def test_framer_opaque_head():
    # head, CH_ID ile PAYLOAD arasına opak önek olarak girer (reliability seq gibi).
    framer = Framer()
    frame = framer.build(ch_id=7, payload=b"\x01\x02", head=b"\x03")
    assert frame[4] == 4  # LEN = ch_id + head + 2 payload
    assert frame[5] == 7  # CH_ID
    assert frame[6] == 3  # HEAD (seq)
    assert frame[7:9] == b"\x01\x02"
    assert frame[9] == wireframe.crc8(bytes((7, 3, 1, 2)))


def test_framer_parser_roundtrip():
    framer = Framer()
    parser = Parser()
    got = []
    parser.on_frame_completed = lambda data: got.append(data)

    frame = framer.build(ch_id=42, payload=b"hello")
    parser.feed(frame)

    assert len(got) == 1
    assert got[0] == bytes((42,)) + b"hello"


def test_parser_resync_after_garbage():
    framer = Framer()
    parser = Parser()
    got = []
    parser.on_frame_completed = lambda data: got.append(data)

    frame = framer.build(ch_id=1, payload=b"\xaa")
    parser.feed(b"\x00\xff garbage \x6d" + frame)

    assert len(got) == 1
    assert got[0] == bytes((1,)) + b"\xaa"


# ── Mesaj serializasyonu ─────────────────────────────────────────────────────


def test_message_roundtrip():
    msg = Twist(Vector3(1.0, 2.0, 3.0), Vector3(-1.0, -2.0, -3.0))
    buf = msg.to_bytes()
    assert len(buf) == Twist.SIZE == 24
    back = Twist.from_bytes(buf)
    assert back.linear.x == 1.0
    assert back.angular.z == -3.0
    assert back == msg


def test_message_from_bytes_too_short():
    assert Float32.from_bytes(b"\x00\x00") is None


# ── Node (düşük seviye, saf ham byte) ────────────────────────────────────────


def test_node_publish_subscribe():
    lb = Loopback()
    node = Node()
    node.transport = lb.transport()

    received = []
    node.subscribe(5, lambda payload: received.append(payload))
    assert node.publish(5, b"\xde\xad")

    node.spin_once()
    assert received == [b"\xde\xad"]


# ── NodeHL (tipli) ───────────────────────────────────────────────────────────


def test_node_hl_typed():
    lb = Loopback()
    node = NodeHL()
    node.transport = lb.transport()

    received = []
    node.create_subscription(Float32, 3, lambda msg: received.append(msg.value))
    pub = node.create_publisher(Float32, 3)
    assert pub.publish(Float32(3.14))

    node.spin_once()
    assert len(received) == 1
    assert abs(received[0] - 3.14) < 1e-6


# ── Reliability: ACK + retransmit (otonom overlay) ──────────────────────────


def test_reliable_ack_clears_pending():
    """İki düğüm; reliable publish, ACK ile temizlenir, sonraki seq artar."""
    lb_a2b = Loopback()  # A -> B
    lb_b2a = Loopback()  # B -> A

    node_a = Node()
    node_a.transport = Transport(
        send_bytes=lb_a2b.send_bytes,
        read_bytes=lb_b2a.read_bytes,
        get_size=lb_b2a.get_size,
        get_time=lambda: 0,
    )
    rel_a = Reliable(node_a)

    node_b = Node()
    node_b.transport = Transport(
        send_bytes=lb_b2a.send_bytes,
        read_bytes=lb_a2b.read_bytes,
        get_size=lb_a2b.get_size,
        get_time=lambda: 0,
    )
    rel_b = Reliable(node_b)

    got = []
    rel_b.subscribe(8, lambda payload: got.append(payload))

    assert rel_a.publish(8, b"\x01")  # seq=1
    node_b.spin_once()  # B veriyi alır + ACK gönderir
    node_a.spin_once()  # A ACK'i alır, ack_pending temizlenir

    assert got == [b"\x01"]
    assert rel_a.can_send(8)            # ACK geldi
    assert rel_a.publish(8, b"\x02")    # yeni seq ile başarılı


def test_reliable_blocks_until_ack():
    """ACK gelmeden ikinci publish reddedilmeli."""
    lb = Loopback()
    node = Node()
    node.transport = lb.transport()
    rel = Reliable(node)

    assert rel.publish(8, b"\x01") is True
    assert rel.can_send(8) is False
    assert rel.publish(8, b"\x02") is False


def test_reliable_retransmit_on_timeout():
    """Timeout aşılınca tick() aynı payload'ı otonom yeniden gönderir."""
    lb = Loopback()
    clock = {"t": 0}
    node = Node()
    node.transport = Transport(
        send_bytes=lb.send_bytes,
        read_bytes=lb.read_bytes,
        get_size=lb.get_size,
        get_time=lambda: clock["t"],
    )
    rel = Reliable(node, timeout_ms=50)

    assert rel.publish(8, b"\x01")  # seq=1, t=0
    sent_after_publish = len(lb.buf)
    assert sent_after_publish > 0

    clock["t"] = 100  # timeout aşıldı
    rel.tick()
    # Otonom yeniden gönderim: wire'a ikinci (özdeş) frame yazılmalı.
    assert len(lb.buf) == 2 * sent_after_publish


def _run_all():
    fns = [v for k, v in sorted(globals().items()) if k.startswith("test_") and callable(v)]
    passed = 0
    for fn in fns:
        fn()
        print(f"  ok  {fn.__name__}")
        passed += 1
    print(f"\n{passed}/{len(fns)} test geçti.")


if __name__ == "__main__":
    _run_all()
