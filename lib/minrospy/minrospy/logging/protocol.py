"""minros logging protokol sabitleri.

Rezerve protokol kanal bloğu:
    249 = reliability ACK
    248 = logging
Bu blok overlay'lere ayrılmıştır; kullanıcı kanalları buraya çakışmamalıdır.

Log frame'i (CH248 üzerinden, payload):
    PAYLOAD = [FLAGS(1)][text/bytes...]
    FLAGS opak bir head önekidir; core anlamını bilmez (reliability seq gibi).

FLAGS bit yerleşimi (1 byte):
    bit 0       LAST   : 1 -> log'un son (veya tek) parçası
    bit 1..3    LEVEL  : seviye 0..4; her parçada taşınır -> parse tekdüze
    bit 4..7    SEQ4   : 0..15 dönen parça sayacı (kayıp/atlama tespiti)

C++ tarafındaki minros::logging::protocol'un portudur.
"""

import enum


class Level(enum.IntEnum):
    DEBUG = 0
    INFO = 1
    WARN = 2
    ERROR = 3
    FATAL = 4


LOG_CHANNEL_ID = 248


def pack_flags(level: Level, seq4: int, last: bool) -> int:
    return ((seq4 & 0x0F) << 4) | ((int(level) & 0x07) << 1) | (0x01 if last else 0x00)


def flag_last(flags: int) -> bool:
    return (flags & 0x01) != 0


def flag_level(flags: int) -> Level:
    return Level((flags >> 1) & 0x07)


def flag_seq(flags: int) -> int:
    return (flags >> 4) & 0x0F
