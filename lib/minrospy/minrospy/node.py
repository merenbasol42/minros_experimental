"""Node — saf ham byte datagram API (kanal bazlı publish/subscribe).

Reliability'den habersizdir. Güvenilirlik isteyen, bu Node'un public API'sini
kullanan reliability.Reliable overlay'ini takar (bkz. reliability/reliable.py).

Transport, 4 callable tutar:
    send_bytes(data: bytes) -> None    — frame'i fiziksel kanala yazar
    read_bytes(n: int) -> bytes        — en fazla n byte okur
    get_size() -> int                  — okunmaya hazır byte sayısı
    get_time() -> int                  — milisaniye cinsinden zaman

Kullanım:
    node = Node()
    node.transport = Transport(send_bytes=..., read_bytes=..., get_size=..., get_time=...)

    node.subscribe(ch_id, lambda payload: ...)   # fn(payload)
    node.publish(ch_id, data)                     # ham payload
    node.publish(ch_id, data, head=bytes([seq]))  # opak head önekli (layered)

    node.spin_once()   # ana döngüde
"""

from collections.abc import Callable
from dataclasses import dataclass

from .core import wireframe
from .core.broker import Broker, ChannelCallback
from .core.framer import Framer
from .core.parser import Parser


@dataclass
class Transport:
    send_bytes: Callable[[bytes], None] | None = None
    read_bytes: Callable[[int], bytes] | None = None
    get_size: Callable[[], int] | None = None
    get_time: Callable[[], int] | None = None


class Node:
    def __init__(self, max_frame_data: int = wireframe.MAX_DATA_LEN):
        self.transport = Transport()

        self._parser = Parser(max_frame_data)
        self._broker = Broker()
        self._framer = Framer(max_frame_data)

        self._parser.on_frame_completed = self._broker.on_frame_completed

    # ── Ana döngü ──────────────────────────────────────────────────────────

    def spin_once(self) -> None:
        self._feed_parser()

    # ── Yayınlama ──────────────────────────────────────────────────────────

    def publish(self, ch_id: int, payload: bytes, head: bytes = b"") -> bool:
        """Ham payload gönder. `head` verilirse opak bir önek olarak eklenir
        (layered protokoller için, örn. reliability seq baytı). Core head'in
        anlamını bilmez; tele CH_ID + head + payload olarak girer.
        """
        if self.transport.send_bytes is None:
            return False
        frame = self._framer.build(ch_id, payload, head)
        if frame is None:
            return False
        self.transport.send_bytes(frame)
        return True

    # ── Abonelik ───────────────────────────────────────────────────────────

    def subscribe(self, ch_id: int, cb: ChannelCallback) -> bool:
        """CH_ID'ye callback kaydeder. cb imzası: fn(payload)."""
        return self._broker.subscribe(ch_id, cb)

    # ── İç yardımcılar ─────────────────────────────────────────────────────

    def _feed_parser(self) -> None:
        if self.transport.get_size is None or self.transport.read_bytes is None:
            return
        n = self.transport.get_size()
        if n <= 0:
            return
        data = self.transport.read_bytes(n)
        if data:
            self._parser.feed(data)
