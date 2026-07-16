"""İlkel (primitive) tipli mesajlar — minros std_msgs portu.

Wire formatı little-endian'dır (struct '<' kullanılır).
"""

import struct

from ..msg_base import MsgBase


class Float32(MsgBase):
    SIZE = 4
    FAMILY_ID = 0x00  # std_msgs ailesi
    TYPE_ID = 0x00  # std_msgs-yerel: FLOAT32

    def __init__(self, value: float = 0.0):
        self.value = value

    def _serialize(self) -> bytes:
        return struct.pack("<f", self.value)

    def _deserialize(self, buf: bytes) -> None:
        (self.value,) = struct.unpack_from("<f", buf)


class Int32(MsgBase):
    SIZE = 4
    FAMILY_ID = 0x00  # std_msgs ailesi
    TYPE_ID = 0x01  # std_msgs-yerel: INT32

    def __init__(self, value: int = 0):
        self.value = value

    def _serialize(self) -> bytes:
        return struct.pack("<i", self.value)

    def _deserialize(self, buf: bytes) -> None:
        (self.value,) = struct.unpack_from("<i", buf)


class Int16(MsgBase):
    SIZE = 2
    FAMILY_ID = 0x00  # std_msgs ailesi
    TYPE_ID = 0x02  # std_msgs-yerel: INT16

    def __init__(self, value: int = 0):
        self.value = value

    def _serialize(self) -> bytes:
        return struct.pack("<h", self.value)

    def _deserialize(self, buf: bytes) -> None:
        (self.value,) = struct.unpack_from("<h", buf)


class Int8(MsgBase):
    SIZE = 1
    FAMILY_ID = 0x00  # std_msgs ailesi
    TYPE_ID = 0x03  # std_msgs-yerel: INT8

    def __init__(self, value: int = 0):
        self.value = value

    def _serialize(self) -> bytes:
        return struct.pack("<b", self.value)

    def _deserialize(self, buf: bytes) -> None:
        (self.value,) = struct.unpack_from("<b", buf)


class UInt32(MsgBase):
    SIZE = 4
    FAMILY_ID = 0x00  # std_msgs ailesi
    TYPE_ID = 0x04  # std_msgs-yerel: UINT32

    def __init__(self, value: int = 0):
        self.value = value

    def _serialize(self) -> bytes:
        return struct.pack("<I", self.value)

    def _deserialize(self, buf: bytes) -> None:
        (self.value,) = struct.unpack_from("<I", buf)


class UInt16(MsgBase):
    SIZE = 2
    FAMILY_ID = 0x00  # std_msgs ailesi
    TYPE_ID = 0x05  # std_msgs-yerel: UINT16

    def __init__(self, value: int = 0):
        self.value = value

    def _serialize(self) -> bytes:
        return struct.pack("<H", self.value)

    def _deserialize(self, buf: bytes) -> None:
        (self.value,) = struct.unpack_from("<H", buf)


class UInt8(MsgBase):
    SIZE = 1
    FAMILY_ID = 0x00  # std_msgs ailesi
    TYPE_ID = 0x06  # std_msgs-yerel: UINT8

    def __init__(self, value: int = 0):
        self.value = value

    def _serialize(self) -> bytes:
        return struct.pack("<B", self.value)

    def _deserialize(self, buf: bytes) -> None:
        (self.value,) = struct.unpack_from("<B", buf)


class Bool(MsgBase):
    SIZE = 1
    FAMILY_ID = 0x00  # std_msgs ailesi
    TYPE_ID = 0x07  # std_msgs-yerel: BOOL

    def __init__(self, value: bool = False):
        self.value = value

    def _serialize(self) -> bytes:
        return struct.pack("<B", 1 if self.value else 0)

    def _deserialize(self, buf: bytes) -> None:
        self.value = buf[0] != 0
