#pragma once

#include "minros/core/wireframe.hpp"
#include "minros/utils/utils.hpp"

namespace minros::core {

    //
    // Framer: Ham payload'ı wire formatına dönüştürür, frame'i dahili buffer'da tutar.
    //
    // Frame yapısı:
    //   [HEADER(4)] [LENGTH(1)] [CH_ID(1)] [HEAD(h)] [PAYLOAD(n)] [CRC(1)]
    //
    // CRC: CRC-8/SMBUS — DATA (CH_ID + HEAD + PAYLOAD) üzerinden.
    // HEAD: opak bir önek (örn. reliability seq baytı). Core anlamını bilmez.
    // Maksimum DATA uzunluğu: MAX_DATA (CH_ID + HEAD + PAYLOAD).
    //
    // Template parametreler:
    //   MAX_DATA — DATA alanının maksimum uzunluğu (varsayılan wireframe::MAX_DATA_LEN).
    //              Küçük tutulursa buf_ belleği doğrudan küçülür.
    //
    // Kullanım:
    //   Framer<> framer;
    //   if (framer.build(ch_id, payload, payload_len)) {       // head'siz
    //       send(framer.data(), framer.size());
    //   }
    //   framer.build(ch_id, head, head_len, payload, payload_len);  // head'li
    //
    template<u8 MAX_DATA = wireframe::MAX_DATA_LEN>
    class Framer {
        static_assert(MAX_DATA >= wireframe::MIN_DATA_LEN &&
                      MAX_DATA <= wireframe::MAX_DATA_LEN,
                      "MAX_DATA: MIN_DATA_LEN..wireframe::MAX_DATA_LEN aralığında olmalı");
    public:
        static constexpr u8 BUFFER_SIZE =
            wireframe::HEADER_SIZE +  // 4 byte
            1u +                      // LENGTH field
            MAX_DATA +                // CH_ID + payload
            1u;                       // CRC byte

        // Frame'i dahili buffer'a yazar (head'siz — yaygın yol).
        bool build(u8 ch_id, const u8* payload, u8 payload_len) {
            return build(ch_id, nullptr, 0, payload, payload_len);
        }

        // Frame'i dahili buffer'a yazar (opak HEAD öneki + payload).
        // DATA = CH_ID + head + payload, toplam en fazla MAX_DATA byte olabilir.
        bool build(u8 ch_id, const u8* head, u8 head_len,
                   const u8* payload, u8 payload_len) {
            // integer promotion: u8'ler int'e yükselir, toplam (maks 511) sarmaz.
            // Sınır kontrolünü u8'e truncate'ten ÖNCE yap.
            if (1u + head_len + payload_len > MAX_DATA) {
                size_ = 0;
                return false;
            }

            u8 idx = 0;

            for (u8 i = 0; i < wireframe::HEADER_SIZE; i++) {
                buf_[idx++] = wireframe::HEADER[i];
            }

            // LENGTH = CH_ID + head + payload
            buf_[idx++] = static_cast<u8>(1u + head_len + payload_len);

            u8 crc = 0;

            buf_[idx++] = ch_id;
            crc = wireframe::crc8_update(crc, ch_id);

            for (u8 i = 0; i < head_len; i++) {       // head_len==0 → çalışmaz, head==nullptr güvenli
                buf_[idx++] = head[i];
                crc = wireframe::crc8_update(crc, head[i]);
            }

            for (u8 i = 0; i < payload_len; i++) {
                buf_[idx++] = payload[i];
                crc = wireframe::crc8_update(crc, payload[i]);
            }

            buf_[idx++] = crc;
            size_ = idx;
            return true;
        }

        u8*       data()  { return buf_; }
        const u8* data()  const { return buf_; }
        u8        size()  const { return size_; }

    private:
        u8 buf_[BUFFER_SIZE];
        u8 size_ = 0;
    };

} // namespace minros::core
