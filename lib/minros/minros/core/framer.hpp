#pragma once

#include "minros/core/wireframe.hpp"
#include "minros/utils/utils.hpp"

namespace minros::core {

    //
    // Framer: Ham payload'ı wire formatına dönüştürür, frame'i dahili buffer'da tutar.
    //
    // Frame yapısı:
    //   [HEADER(4)] [LENGTH(1)] [CH_ID(1)] [SEQ (1)] [PAYLOAD(n)] [CRC(1)]
    //
    // CRC: CRC-8/SMBUS — DATA (CH_ID + SEQ + PAYLOAD) üzerinden.
    // Maksimum payload uzunluğu: MAX_DATA - 2 (wireframe::MIN_DATA)
    //
    // Template parametreler:
    //   MAX_DATA — DATA alanının maksimum uzunluğu (varsayılan wireframe::MAX_DATA_LEN).
    //              Küçük tutulursa buf_ belleği doğrudan küçülür.
    //
    // Kullanım:
    //   Framer<> framer;
    //   if (framer.build(seq, ch_id, payload, payload_len)) {
    //       send(framer.data(), framer.size());
    //   }
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

        // Frame'i dahili buffer'a yazar.
        // payload uzunluğu MAX_DATA-1 payload
        bool build(u8 ch_id, u8 seq, const u8* payload, u8 payload_len) {
            if (payload_len + 2u > MAX_DATA) {
                size_ = 0;
                return false;
            }

            u8 idx = 0;

            for (u8 i = 0; i < wireframe::HEADER_SIZE; i++) {
                buf_[idx++] = wireframe::HEADER[i];
            }

            buf_[idx++] = static_cast<u8>(payload_len + 2u); // LENGTH = SEQ + CH_ID + payload

            u8 crc = 0;

            buf_[idx++] = ch_id;
            crc = wireframe::crc8_update(crc, ch_id);
            
            buf_[idx++] = seq;
            crc = wireframe::crc8_update(crc, seq);

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
