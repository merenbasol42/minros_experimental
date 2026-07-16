"""Alan tipi (field type) kimlikleri — bir mesajın tek bir alanının tipi.

Mesajın bütününü tanımlayan FAMILY_ID/TYPE_ID'den farklıdır; bu enum alan
seviyesindedir. Şu an std_msgs tarafından kullanılmıyor; parametre overlay'i
veya ileride bir şema/codegen katmanı için ayrık tutuluyor.

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
