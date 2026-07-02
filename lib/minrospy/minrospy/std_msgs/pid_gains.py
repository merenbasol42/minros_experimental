"""PidGains — kp, ki, kd (float32). 12 byte. Wire formatı little-endian."""

import struct

from .field_type import FieldType
from .msg_base import MsgBase


class PidGains(MsgBase):
    SIZE = 12  # 3 * 4
    TYPE_ID = 0x0B
    FIELD_COUNT = 3
    FIELD_NAMES = "kp,ki,kd"
    FIELD_TYPES = (FieldType.F32, FieldType.F32, FieldType.F32)

    def __init__(self, kp: float = 0.0, ki: float = 0.0, kd: float = 0.0):
        self.kp = kp
        self.ki = ki
        self.kd = kd

    def _serialize(self) -> bytes:
        return struct.pack("<3f", self.kp, self.ki, self.kd)

    def _deserialize(self, buf: bytes) -> None:
        self.kp, self.ki, self.kd = struct.unpack_from("<3f", buf)
