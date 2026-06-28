"""introduce protokolü alan tipi kimlikleri (FIELD_TYPES değerleri).

C++ tarafındaki sayısal değerlerle birebir uyumludur.
"""

import enum


class FieldType(enum.IntEnum):
    U8 = 0
    U16 = 1
    U32 = 2
    I8 = 3
    I16 = 4
    I32 = 5
    F32 = 6
    F64 = 7
    BOOL = 8
