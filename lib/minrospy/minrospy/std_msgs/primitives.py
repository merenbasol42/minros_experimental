"""İlkel (primitive) tipli mesajlar — minros std_msgs portu.

Wire formatı little-endian'dır (struct '<' kullanılır).
"""

import struct

from .field_type import FieldType
from .msg_base import MsgBase


class Float32(MsgBase):
    SIZE = 4
    TYPE_ID = 0x00
    FIELD_COUNT = 1
    FIELD_NAMES = "value"
    FIELD_TYPES = (FieldType.F32,)

    def __init__(self, value: float = 0.0):
        self.value = value

    def _serialize(self) -> bytes:
        return struct.pack("<f", self.value)

    def _deserialize(self, buf: bytes) -> None:
        (self.value,) = struct.unpack_from("<f", buf)


class Int32(MsgBase):
    SIZE = 4
    TYPE_ID = 0x01
    FIELD_COUNT = 1
    FIELD_NAMES = "value"
    FIELD_TYPES = (FieldType.I32,)

    def __init__(self, value: int = 0):
        self.value = value

    def _serialize(self) -> bytes:
        return struct.pack("<i", self.value)

    def _deserialize(self, buf: bytes) -> None:
        (self.value,) = struct.unpack_from("<i", buf)


class Int16(MsgBase):
    SIZE = 2
    TYPE_ID = 0x02
    FIELD_COUNT = 1
    FIELD_NAMES = "value"
    FIELD_TYPES = (FieldType.I16,)

    def __init__(self, value: int = 0):
        self.value = value

    def _serialize(self) -> bytes:
        return struct.pack("<h", self.value)

    def _deserialize(self, buf: bytes) -> None:
        (self.value,) = struct.unpack_from("<h", buf)


class Int8(MsgBase):
    SIZE = 1
    TYPE_ID = 0x03
    FIELD_COUNT = 1
    FIELD_NAMES = "value"
    FIELD_TYPES = (FieldType.I8,)

    def __init__(self, value: int = 0):
        self.value = value

    def _serialize(self) -> bytes:
        return struct.pack("<b", self.value)

    def _deserialize(self, buf: bytes) -> None:
        (self.value,) = struct.unpack_from("<b", buf)


class UInt32(MsgBase):
    SIZE = 4
    TYPE_ID = 0x04
    FIELD_COUNT = 1
    FIELD_NAMES = "value"
    FIELD_TYPES = (FieldType.U32,)

    def __init__(self, value: int = 0):
        self.value = value

    def _serialize(self) -> bytes:
        return struct.pack("<I", self.value)

    def _deserialize(self, buf: bytes) -> None:
        (self.value,) = struct.unpack_from("<I", buf)


class UInt16(MsgBase):
    SIZE = 2
    TYPE_ID = 0x05
    FIELD_COUNT = 1
    FIELD_NAMES = "value"
    FIELD_TYPES = (FieldType.U16,)

    def __init__(self, value: int = 0):
        self.value = value

    def _serialize(self) -> bytes:
        return struct.pack("<H", self.value)

    def _deserialize(self, buf: bytes) -> None:
        (self.value,) = struct.unpack_from("<H", buf)


class UInt8(MsgBase):
    SIZE = 1
    TYPE_ID = 0x06
    FIELD_COUNT = 1
    FIELD_NAMES = "value"
    FIELD_TYPES = (FieldType.U8,)

    def __init__(self, value: int = 0):
        self.value = value

    def _serialize(self) -> bytes:
        return struct.pack("<B", self.value)

    def _deserialize(self, buf: bytes) -> None:
        (self.value,) = struct.unpack_from("<B", buf)


class Bool(MsgBase):
    SIZE = 1
    TYPE_ID = 0x07
    FIELD_COUNT = 1
    FIELD_NAMES = "value"
    FIELD_TYPES = (FieldType.BOOL,)

    def __init__(self, value: bool = False):
        self.value = value

    def _serialize(self) -> bytes:
        return struct.pack("<B", 1 if self.value else 0)

    def _deserialize(self, buf: bytes) -> None:
        self.value = buf[0] != 0
