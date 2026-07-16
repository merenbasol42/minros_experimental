"""Vector3 — 3 adet float32 (x, y, z). 12 byte. Wire formatı little-endian."""

import struct

from ..msg_base import MsgBase


class Vector3(MsgBase):
    SIZE = 12  # 3 * 4
    FAMILY_ID = 0x01  # geometry_msgs ailesi
    TYPE_ID = 0x00  # geometry_msgs-yerel: VECTOR3

    def __init__(self, x: float = 0.0, y: float = 0.0, z: float = 0.0):
        self.x = x
        self.y = y
        self.z = z

    def _serialize(self) -> bytes:
        return struct.pack("<3f", self.x, self.y, self.z)

    def _deserialize(self, buf: bytes) -> None:
        self.x, self.y, self.z = struct.unpack_from("<3f", buf)
