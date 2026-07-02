"""minros wire formatı — minrospy portu.

Frame yapısı:
    HEADER       : {0x6D, 0x72, 0x6F, 0x73}  ('m','r','o','s') — senkronizasyon
    LEN          : DATA uzunluğu (2..249)
    DATA         : DATA (LEN kadar)
        CH_ID    : DATA[0] — kanal kimliği
        PAYLOAD  : DATA[1..LEN-1] — kullanıcı verisi (1..248 byte)
    CRC          : CRC-8/SMBUS (poly=0x07, init=0x00) — DATA byte'ları üzerinden

Not: core katmanı SEQ bilmez. Güvenilirlik (reliability) katmanı, kendi sıra
     numarasını PAYLOAD'ın önüne opak bir baytlık önek olarak koyar; core bunu
     opak veri olarak görür.

Endianness: wire formatı little-endian'dır (bkz. std_msgs serialize/deserialize).
"""

VERSION = (0x00, 0x00, 0x01, 0x00)  # major.minor.patch.build

HEADER = bytes((0x6D, 0x72, 0x6F, 0x73))
HEADER_SIZE = len(HEADER)

# DATA alanı sınırları.
# BUFFER_SIZE = HEADER(4) + LEN(1) + DATA(249) + CRC(1) = 255 (u8 max)
MAX_DATA_LEN = 249
MAX_PAYLOAD = MAX_DATA_LEN - 1  # 248 byte (CH_ID çıkarıldı)
MIN_PAYLOAD = 1
MIN_DATA_LEN = 1 + MIN_PAYLOAD  # en az CH_ID(1) + MIN_PAYLOAD


def crc8_update(crc: int, byte: int) -> int:
    """CRC-8/SMBUS — polinom 0x07, init 0x00, XOR-out 0x00.

    Parser ve Framer tarafından paylaşılır; tablo gerektirmez.
    """
    crc ^= byte
    for _ in range(8):
        if crc & 0x80:
            crc = ((crc << 1) ^ 0x07) & 0xFF
        else:
            crc = (crc << 1) & 0xFF
    return crc


def crc8(data: bytes) -> int:
    """Bir byte dizisi üzerinden CRC-8/SMBUS hesaplar."""
    crc = 0
    for b in data:
        crc = crc8_update(crc, b)
    return crc
