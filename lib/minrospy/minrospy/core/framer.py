"""Framer: ham payload'ı wire formatına dönüştürür.

Frame yapısı:
    [HEADER(4)] [LENGTH(1)] [CH_ID(1)] [HEAD(h)] [PAYLOAD(n)] [CRC(1)]

CRC: CRC-8/SMBUS — DATA (CH_ID + HEAD + PAYLOAD) üzerinden.
HEAD: opak bir önek (örn. reliability seq baytı). Core anlamını bilmez.
Maksimum DATA uzunluğu: MAX_DATA (CH_ID + HEAD + PAYLOAD).

Kullanım:
    framer = Framer()
    frame = framer.build(ch_id, payload)                     # head'siz (yaygın yol)
    frame = framer.build(ch_id, payload, head=bytes([seq]))  # opak head önekli
    if frame is not None:
        transport.send_bytes(frame)
"""

from . import wireframe


class Framer:
    def __init__(self, max_data: int = wireframe.MAX_DATA_LEN):
        if not (wireframe.MIN_DATA_LEN <= max_data <= wireframe.MAX_DATA_LEN):
            raise ValueError("max_data: MIN_DATA_LEN..MAX_DATA_LEN aralığında olmalı")
        self.max_data = max_data

    def build(self, ch_id: int, payload: bytes, head: bytes = b"") -> bytes | None:
        """Frame'i kurar ve byte olarak döner; sınır aşılırsa None döner.

        DATA = CH_ID + head + payload. `head`, core'un anlamını bilmediği opak
        bir önektir (reliability seq baytı gibi).
        """
        data_len = 1 + len(head) + len(payload)
        if data_len > self.max_data:
            return None

        data = bytes((ch_id & 0xFF,)) + bytes(head) + bytes(payload)
        crc = wireframe.crc8(data)

        return wireframe.HEADER + bytes((len(data),)) + data + bytes((crc,))
