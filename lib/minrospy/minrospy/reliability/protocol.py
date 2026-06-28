"""minros reliability protokol sabitleri.

ACK mesaj tanımı (CH249 üzerinden):
    RESP  : 1 byte = 0x06 (response tipi, ASCII ACK)
    CH_ID : 1 byte (ACK'lenen kanal)
    SEQ   : 1 byte (ACK'lenen sequence numarası)

RESP alanı ileride NACK gibi başka yanıt tipleri eklenebilsin diye vardır.
"""

import enum


class ResponseType(enum.IntEnum):
    ACK = 0x06


ACK_CHANNEL_ID = 249
