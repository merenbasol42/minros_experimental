"""Reliable — minros güvenilirlik (reliability) overlay'i.

Node'un public pub/sub API'sini kullanan bağımsız bir kullanıcıdır. Core'a
hiçbir şey eklemez: seq'i, payload'ın önüne opak bir baytlık önek olarak koyar
ve ACK'i normal bir kanaldan (CH249) yollar. NodeHL gerektirmez — ham Node ile
de kullanılabilir. C++ tarafındaki reliability::Reliable overlay'inin portudur.

Stop-and-wait (window = 1): kanal başına aynı anda en fazla 1 uçuştaki frame.
Timeout otonomdur: tick() tutulan payload'ı kendisi yeniden gönderir (retransmit
callback'i YOKTUR). Python'da bytes immutable olduğundan payload kopyası tutulur.

Kullanım:
    node = Node()
    rel = Reliable(node)                  # node'a takılır, ACK kanalına abone olur

    rel.subscribe(ch, lambda payload: ...)   # seq/dedup/ACK gizli

    # loop:
    node.spin_once()
    rel.tick()

    if rel.can_send(ch):
        rel.publish(ch, data)
"""

import enum
from collections.abc import Callable
from dataclasses import dataclass

from . import protocol

# fn(payload) — seq önekı ayıklanmış, kullanıcı verisi
DataCallback = Callable[[bytes], None]


class ErrorCode(enum.Enum):
    MAX_RETRIED = 0


@dataclass
class _PubEntry:
    ch: int
    payload: bytes = b""
    sent_at_ms: int = 0
    seq: int = 0
    retries: int = 0
    ack_pending: bool = False


@dataclass
class _SubEntry:
    ch: int
    cb: DataCallback
    last_seq: int = 0xFF  # geçersiz başlangıç: ilk mesaj her zaman yeni


class Reliable:
    def __init__(
        self,
        node,
        max_pub: int = 4,
        max_sub: int = 4,
        max_retry: int = 3,
        timeout_ms: int = 50,
    ):
        self._node = node
        self._max_pub = max_pub
        self._max_sub = max_sub
        self._max_retry = max_retry
        self._timeout_ms = timeout_ms

        self._pubs: list[_PubEntry] = []
        self._subs: list[_SubEntry] = []
        # fn(ch_id, ErrorCode) — MAX_RETRIED aşılınca çağrılır
        self.on_err: Callable[[int, ErrorCode], None] | None = None

        self._node.subscribe(protocol.ACK_CHANNEL_ID, self._ack_thunk)

    # ── Güvenilir subscriber ───────────────────────────────────────────────

    def subscribe(self, ch: int, cb: DataCallback) -> bool:
        """Dedup + otomatik ACK içeride; cb yalnızca yeni mesajda çağrılır."""
        if len(self._subs) >= self._max_sub or cb is None:
            return False

        entry = _SubEntry(ch=ch, cb=cb)
        self._subs.append(entry)
        return self._node.subscribe(ch, self._make_rx(entry))

    # ── Güvenilir publisher ────────────────────────────────────────────────

    def register_pub(self, ch: int) -> bool:
        """Pub slotunu önceden ayır (opsiyonel — publish ilk çağrıda da ayırır)."""
        return self._get_or_add_pub(ch) is not None

    def can_send(self, ch: int) -> bool:
        """Bu kanalda yeni reliable mesaj gönderilebilir mi (önceki ACK'lendi mi)?"""
        p = self._find_pub(ch)
        return p is None or not p.ack_pending

    def publish(self, ch: int, payload: bytes) -> bool:
        """Güvenilir gönder. ack_pending ise False döner."""
        p = self._get_or_add_pub(ch)
        if p is None or p.ack_pending:
            return False

        p.seq = (p.seq + 1) & 0xFF
        p.payload = bytes(payload)   # immutable kopya — retransmit backing
        p.ack_pending = True
        p.retries = 0
        p.sent_at_ms = self._now()
        return self._send(p)

    # ── Periyodik timeout kontrolü ─────────────────────────────────────────

    def tick(self) -> None:
        """Her ana döngüde çağır. Timeout aşan kanalları otonom yeniden gönderir."""
        t = self._now()
        for p in self._pubs:
            if not p.ack_pending:
                continue
            if (t - p.sent_at_ms) < self._timeout_ms:
                continue

            if p.retries >= self._max_retry:
                p.ack_pending = False
                p.retries = 0
                if self.on_err is not None:
                    self.on_err(p.ch, ErrorCode.MAX_RETRIED)
                continue

            p.retries += 1
            p.sent_at_ms = t
            self._send(p)   # aynı payload'dan otonom yeniden gönderim

    # ── İç yardımcılar ─────────────────────────────────────────────────────

    def _now(self) -> int:
        return self._node.transport.get_time()

    def _send(self, p: _PubEntry) -> bool:
        # seq, payload önüne opak 1-baytlık HEAD: tele [CH_ID][SEQ][payload].
        return self._node.publish(p.ch, p.payload, head=bytes((p.seq,)))

    def _send_ack(self, ch: int, seq: int) -> None:
        buf = bytes((int(protocol.ResponseType.ACK), ch & 0xFF, seq & 0xFF))
        self._node.publish(protocol.ACK_CHANNEL_ID, buf)  # ACK unreliable

    def _make_rx(self, entry: _SubEntry) -> DataCallback:
        def rx(payload: bytes) -> None:
            if len(payload) < 1:
                return
            seq = payload[0]
            self._send_ack(entry.ch, seq)   # duplicate olsa da ACK at
            if seq != entry.last_seq:
                entry.last_seq = seq
                if entry.cb is not None:
                    entry.cb(payload[1:])
        return rx

    def _ack_thunk(self, payload: bytes) -> None:
        # ACK alımı (CH249). Payload: [RESP][acked_ch][acked_seq].
        if len(payload) < 3:
            return
        if payload[0] != int(protocol.ResponseType.ACK):
            return
        acked_ch, acked_seq = payload[1], payload[2]
        p = self._find_pub(acked_ch)
        if p is not None and p.ack_pending and p.seq == acked_seq:
            p.ack_pending = False
            p.retries = 0

    def _find_pub(self, ch: int) -> _PubEntry | None:
        for p in self._pubs:
            if p.ch == ch:
                return p
        return None

    def _get_or_add_pub(self, ch: int) -> _PubEntry | None:
        p = self._find_pub(ch)
        if p is not None:
            return p
        if len(self._pubs) >= self._max_pub:
            return None
        p = _PubEntry(ch=ch)
        self._pubs.append(p)
        return p
