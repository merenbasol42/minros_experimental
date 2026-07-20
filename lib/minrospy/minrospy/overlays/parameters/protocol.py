"""minros parameters overlay — protokol sabitleri.

Rezerve kanal bloğu:
    249 = reliability ACK
    248 = logging
    247 = parameters REQ  (host → düğüm : GET, SET)
    246 = parameters RES  (düğüm → host : VALUE, ERR)

İki kanal, reliability'nin "kanal başına tek publisher" sözleşmesini sağlar.

Frame'ler (payload):
    GET   : [OP=0x01][PARAM_ID]
    SET   : [OP=0x02][PARAM_ID][FAMILY_ID][TYPE_ID][msg bytes...]
    VALUE : [OP=0x03][PARAM_ID][FAMILY_ID][TYPE_ID][msg bytes...]
    ERR   : [OP=0x04][PARAM_ID][CODE]

Değer tipi [FAMILY_ID][TYPE_ID] mesaj-tip tanımlayıcısıdır (primitive + kompozit).
Ayrıntı: lib/minros/minros/overlays/parameters/parameters-protocol.md

C++ tarafındaki parameters_protocol.hpp ile birebir uyumludur.
"""

import enum

PARAM_REQ_CHANNEL_ID = 247  # host → düğüm
PARAM_RES_CHANNEL_ID = 246  # düğüm → host


class OpCode(enum.IntEnum):
    GET = 0x01
    SET = 0x02
    VALUE = 0x03
    ERR = 0x04


class ErrCode(enum.IntEnum):
    UNKNOWN_ID = 0x00
    TYPE_MISMATCH = 0x01
    READ_ONLY = 0x02
    BAD_LENGTH = 0x03
