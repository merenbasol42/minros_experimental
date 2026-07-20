"""minros parameters overlay (Python).

İki taraf:
    ParamServer — düğüm tarafı registry (C++ Params'ın karşılığı). PARAM_REQ'e
                  abone olur, GET/SET işler, PARAM_RES'ten VALUE/ERR yollar.
    ParamClient — host tarafı. get()/set() ile REQ yollar, VALUE/ERR'i çözer.

Her ikisi de bir "kanal katmanı" (RawNode veya reliability.Reliable) üzerinden
çalışır; katman subscribe(ch, cb) ve publish(ch, payload) sağlar.

Değerler mesajdır (MsgBase); tip [FAMILY_ID][TYPE_ID] ile taşınır. Primitive'ler
de birer mesajdır (Float32, …), kompozitler de (PidGains, Vector3, …).
"""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from typing import Any

from . import protocol


def default_type_map() -> dict[tuple[int, int], type]:
    """(FAMILY_ID, TYPE_ID) -> mesaj sınıfı — bilinen tüm std/geometry tipleri.

    ParamClient, VALUE frame'lerini bu haritayla çözer.
    """
    from ...interfaces import geometry_msgs, std_msgs

    classes = [
        std_msgs.Float32, std_msgs.Int32, std_msgs.Int16, std_msgs.Int8,
        std_msgs.UInt32, std_msgs.UInt16, std_msgs.UInt8, std_msgs.Bool,
        std_msgs.PidGains,
        geometry_msgs.Vector3, geometry_msgs.Quaternion, geometry_msgs.Twist,
    ]
    return {(c.FAMILY_ID, c.TYPE_ID): c for c in classes}


# ── Düğüm tarafı ─────────────────────────────────────────────────────────────


@dataclass
class _Entry:
    cls: type
    msg: Any
    read_only: bool


class ParamServer:
    def __init__(self, channel):
        self._ch = channel
        self._entries: dict[int, _Entry] = {}
        channel.subscribe(protocol.PARAM_REQ_CHANNEL_ID, self._on_req)

    def register_param(self, id: int, msg, read_only: bool = False) -> None:
        """id'yi bir mesaj örneğiyle kaydeder (mevcut değeri tutar)."""
        self._entries[id] = _Entry(type(msg), msg, read_only)

    def value(self, id: int):
        """Kayıtlı parametrenin güncel değerini döndürür (mesaj örneği)."""
        return self._entries[id].msg

    # ── İç ──
    def _send_value(self, id: int, e: _Entry) -> None:
        head = bytes([protocol.OpCode.VALUE, id, e.cls.FAMILY_ID, e.cls.TYPE_ID])
        self._ch.publish(protocol.PARAM_RES_CHANNEL_ID, head + e.msg.to_bytes())

    def _send_err(self, id: int, code: protocol.ErrCode) -> None:
        self._ch.publish(
            protocol.PARAM_RES_CHANNEL_ID, bytes([protocol.OpCode.ERR, id, code])
        )

    def _on_req(self, payload: bytes) -> None:
        if len(payload) < 2:
            return
        op, pid = payload[0], payload[1]
        e = self._entries.get(pid)

        if op == protocol.OpCode.GET:
            if e is None:
                self._send_err(pid, protocol.ErrCode.UNKNOWN_ID)
                return
            self._send_value(pid, e)

        elif op == protocol.OpCode.SET:
            if len(payload) < 4:
                return
            if e is None:
                self._send_err(pid, protocol.ErrCode.UNKNOWN_ID)
                return
            if e.read_only:
                self._send_err(pid, protocol.ErrCode.READ_ONLY)
                return
            if payload[2] != e.cls.FAMILY_ID or payload[3] != e.cls.TYPE_ID:
                self._send_err(pid, protocol.ErrCode.TYPE_MISMATCH)
                return
            body = payload[4:]
            msg = e.cls.from_bytes(body)
            if msg is None:  # SIZE'dan kısa
                self._send_err(pid, protocol.ErrCode.BAD_LENGTH)
                return
            e.msg = msg
            self._send_value(pid, e)
        # VALUE/ERR/bilinmeyen: düğümde yoksay


# ── Host tarafı ──────────────────────────────────────────────────────────────


class ParamClient:
    def __init__(self, channel, type_map: dict[tuple[int, int], type] | None = None):
        self._ch = channel
        self._types = type_map if type_map is not None else default_type_map()
        self.on_value: Callable[[int, Any], None] | None = None
        self.on_error: Callable[[int, int], None] | None = None
        self.last_value: tuple[int, Any] | None = None
        self.last_error: tuple[int, int] | None = None
        channel.subscribe(protocol.PARAM_RES_CHANNEL_ID, self._on_res)

    def get(self, id: int) -> None:
        self._ch.publish(protocol.PARAM_REQ_CHANNEL_ID, bytes([protocol.OpCode.GET, id]))

    def set(self, id: int, msg) -> None:
        head = bytes([protocol.OpCode.SET, id, msg.FAMILY_ID, msg.TYPE_ID])
        self._ch.publish(protocol.PARAM_REQ_CHANNEL_ID, head + msg.to_bytes())

    # ── İç ──
    def _on_res(self, payload: bytes) -> None:
        if len(payload) < 2:
            return
        op, pid = payload[0], payload[1]

        if op == protocol.OpCode.VALUE:
            if len(payload) < 4:
                return
            cls = self._types.get((payload[2], payload[3]))
            msg = cls.from_bytes(payload[4:]) if cls is not None else None
            self.last_value = (pid, msg)
            if self.on_value is not None:
                self.on_value(pid, msg)

        elif op == protocol.OpCode.ERR:
            if len(payload) < 3:
                return
            self.last_error = (pid, payload[2])
            if self.on_error is not None:
                self.on_error(pid, payload[2])
