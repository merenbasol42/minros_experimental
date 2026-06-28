"""MsgBase — tipli mesajlar için temel sınıf.

C++ tarafındaki CRTP MsgBase'in Python karşılığı. Her mesaj tipi şu sınıf
özniteliklerini tanımlar:
    SIZE        : wire üzerindeki sabit byte uzunluğu
    TYPE_ID     : introduce protokolü mesaj tip kimliği
    FIELD_COUNT : alan sayısı
    FIELD_NAMES : "alan1,alan2,..." formatında alan adları
    FIELD_TYPES : (FieldType, ...) — alan tipleri

ve iki örnek metodu:
    _serialize() -> bytes
    _deserialize(buf: bytes) -> None

Wire formatı little-endian'dır.
"""

from __future__ import annotations


class MsgBase:
    SIZE: int = 0
    TYPE_ID: int = 0
    FIELD_COUNT: int = 0
    FIELD_NAMES: str = ""
    FIELD_TYPES: tuple[int, ...] = ()

    # ── Serializasyon API ──────────────────────────────────────────────────

    def to_bytes(self) -> bytes:
        return self._serialize()

    @classmethod
    def from_bytes(cls, buf: bytes) -> "MsgBase | None":
        """buf yetersizse None döner; aksi halde yeni mesaj örneği döner."""
        if len(buf) < cls.SIZE:
            return None
        msg = cls()
        msg._deserialize(buf)
        return msg

    # ── Tip introspeksiyonu ────────────────────────────────────────────────

    @classmethod
    def size(cls) -> int:
        return cls.SIZE

    @classmethod
    def type_id(cls) -> int:
        return cls.TYPE_ID

    @classmethod
    def field_count(cls) -> int:
        return cls.FIELD_COUNT

    @classmethod
    def field_names(cls) -> str:
        return cls.FIELD_NAMES

    @classmethod
    def field_types(cls) -> tuple[int, ...]:
        return cls.FIELD_TYPES

    # ── Alt sınıfların doldurduğu metodlar ─────────────────────────────────

    def _serialize(self) -> bytes:
        raise NotImplementedError

    def _deserialize(self, buf: bytes) -> None:
        raise NotImplementedError

    # ── Kolaylık ───────────────────────────────────────────────────────────

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, type(self)):
            return NotImplemented
        return self.to_bytes() == other.to_bytes()
