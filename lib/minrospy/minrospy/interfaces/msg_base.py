"""MsgBase — tipli mesajlar için temel sınıf.

C++ tarafındaki CRTP MsgBase'in Python karşılığı. Her mesaj tipi şu sınıf
özniteliklerini tanımlar:
    SIZE      : wire üzerindeki sabit byte uzunluğu
    FAMILY_ID : mesaj ailesi (paket) kimliği
    TYPE_ID   : aile içindeki mesaj kimliği (aile-yerel)

ve iki örnek metodu:
    _serialize() -> bytes
    _deserialize(buf: bytes) -> None

Wire formatı little-endian'dır.

Mesaj tipi kimliği iki parçalıdır: [FAMILY_ID][TYPE_ID]. FAMILY_ID açık bir
kayıt uzayıdır (kapalı enum değil); numaralandırma aralık şemasıyla yönetilir:
    0x00–0x7F  resmi / rezerve aileler — proje tahsis eder
               (std_msgs = 0x00, geometry_msgs = 0x01, sensor_msgs = 0x02, ...)
    0x80–0xFF  özel kullanım (private) — herkes koordinasyonsuz kullanır;
               resmi aileler bu bloğu ASLA almaz → çakışma garantili yok.
"""

from __future__ import annotations


class MsgBase:
    SIZE: int = 0
    FAMILY_ID: int = 0
    TYPE_ID: int = 0

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

    # ── Tip kimliği ────────────────────────────────────────────────────────

    @classmethod
    def size(cls) -> int:
        return cls.SIZE

    @classmethod
    def family_id(cls) -> int:
        return cls.FAMILY_ID

    @classmethod
    def type_id(cls) -> int:
        return cls.TYPE_ID

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
