#pragma once

#include "minros/utils/utils.hpp"

namespace minros::core::wireframe {

    constexpr u8 VERSION[4] = {0x00, 0x00, 0x01, 0x00};  // major.minor.patch.build

    //
    //  Frame yapısı:
    //      HEADER       : {0x6D, 0x72, 0x6F, 0x73}  ('m','r','o','s') — senkronizasyon
    //      LEN          : DATA uzunluğu (2..249)
    //      DATA         : DATA (LEN kadar)
    //          CH_ID    : DATA[0] — kanal kimliği
    //          SEQ      : DATA[1] — gönderici sıra numarası
    //          PAYLOAD  : DATA[2..LEN-1] — kullanıcı verisi (0..247 byte)    
    //      CRC          : CRC-8/SMBUS (poly=0x07, init=0x00) — DATA byte'ları üzerinden
    //
    //
    //  Endianness: wire formatı little-endian. std_msgs katmanı dönüşümü
    //              utils::endian::store_le / load_le ile otomatik yönetir.
    //

    constexpr u8 HEADER_SIZE   = 4u;
    constexpr u8 HEADER[HEADER_SIZE] = {0x6D, 0x72, 0x6F, 0x73};

    // DATA alanı sınırları
    // BUFFER_SIZE = HEADER(4) + LEN(1) + DATA(249) + CRC(1) = 255 (u8 max)
    constexpr u8 MAX_DATA_LEN  = 249u;
    constexpr u8 MAX_PAYLOAD   = MAX_DATA_LEN - 2;  // 247 byte
    constexpr u8 MIN_PAYLOAD   = 1u;
    constexpr u8 MIN_DATA_LEN  = 2u + MIN_PAYLOAD;    // en az SEQ(1) + CH_ID(1) + MIN_PAYLOAD 

    // ── CRC-8/SMBUS ───────────────────────────────────────────────────────────
    // Polinom: 0x07, Init: 0x00, XOR-out: 0x00
    // Parser ve Framer tarafından paylaşılır; tablo gerektirmez.
    inline constexpr u8 crc8_update(u8 crc, u8 byte) noexcept {
        crc ^= byte;
        for (u8 i = 0; i < 8u; i++) {
            crc = (crc & 0x80u)
                ? static_cast<u8>((crc << 1u) ^ 0x07u)
                : static_cast<u8>(crc << 1u);
        }
        return crc;
    }

} // namespace minros::core::wireframe
