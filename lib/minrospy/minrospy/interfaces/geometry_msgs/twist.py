"""Twist — linear (Vector3) + angular (Vector3). 24 byte. Wire formatı little-endian."""

from ..msg_base import MsgBase
from .vector3 import Vector3


class Twist(MsgBase):
    SIZE = 24  # 2 * Vector3.SIZE
    FAMILY_ID = 0x01  # geometry_msgs ailesi
    TYPE_ID = 0x02  # geometry_msgs-yerel: TWIST

    def __init__(self, linear: Vector3 | None = None, angular: Vector3 | None = None):
        self.linear = linear if linear is not None else Vector3()
        self.angular = angular if angular is not None else Vector3()

    def _serialize(self) -> bytes:
        return self.linear.to_bytes() + self.angular.to_bytes()

    def _deserialize(self, buf: bytes) -> None:
        self.linear = Vector3.from_bytes(buf[: Vector3.SIZE])
        self.angular = Vector3.from_bytes(buf[Vector3.SIZE : 2 * Vector3.SIZE])
