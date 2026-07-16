"""PidGains — kp, ki, kd (float32). 12 byte. Wire formatı little-endian."""

import struct

from ..msg_base import MsgBase


class PidGains(MsgBase):
    SIZE = 12  # 3 * 4
    FAMILY_ID = 0x00  # std_msgs ailesi
    TYPE_ID = 0x0B  # std_msgs-yerel: PID_GAINS

    def __init__(self, kp: float = 0.0, ki: float = 0.0, kd: float = 0.0):
        self.kp = kp
        self.ki = ki
        self.kd = kd

    def _serialize(self) -> bytes:
        return struct.pack("<3f", self.kp, self.ki, self.kd)

    def _deserialize(self, buf: bytes) -> None:
        self.kp, self.ki, self.kd = struct.unpack_from("<3f", buf)
