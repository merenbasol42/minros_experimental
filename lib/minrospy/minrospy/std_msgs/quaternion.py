"""Quaternion — 4 adet float32 (x, y, z, w). 16 byte. Wire formatı little-endian."""

import struct

from .field_type import FieldType
from .msg_base import MsgBase


class Quaternion(MsgBase):
    SIZE = 16  # 4 * 4
    TYPE_ID = 0x09
    FIELD_COUNT = 4
    FIELD_NAMES = "x,y,z,w"
    FIELD_TYPES = (FieldType.F32, FieldType.F32, FieldType.F32, FieldType.F32)

    def __init__(self, x: float = 0.0, y: float = 0.0, z: float = 0.0, w: float = 1.0):
        self.x = x
        self.y = y
        self.z = z
        self.w = w

    def _serialize(self) -> bytes:
        return struct.pack("<4f", self.x, self.y, self.z, self.w)

    def _deserialize(self, buf: bytes) -> None:
        self.x, self.y, self.z, self.w = struct.unpack_from("<4f", buf)
