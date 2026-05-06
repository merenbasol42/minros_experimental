#pragma once

#include <minros/utils/utils.hpp>
#include <minros/core/wireframe.hpp>


namespace minros::core {

    // Checksum: CRC-8/SMBUS (polinom 0x07, init 0x00).
    // wireframe::crc8_update paylaşılan implementasyon — Framer ile tutarlı.
    //
    // Template parametreler:
    //   MAX_DATA — DATA alanının maksimum uzunluğu (varsayılan wireframe::MAX_DATA_LEN).
    //              Küçük tutulursa buffer belleği doğrudan küçülür.
    //
    template<u8 MAX_DATA = wireframe::MAX_DATA_LEN>
    class Parser {
        static_assert(MAX_DATA >= wireframe::MIN_DATA_LEN &&
                      MAX_DATA <= wireframe::MAX_DATA_LEN,
                      "MAX_DATA: MIN_DATA_LEN..wireframe::MAX_DATA_LEN aralığında olmalı");

        static constexpr u8 BUFFER_SIZE =
            wireframe::HEADER_SIZE +
            1u /* length field */  +
            MAX_DATA               +
            1u /* checksum field */;

    public:
        // Parser hata kodları.
        //   INVALID_LENGTH — length alanı MIN_DATA_LEN'den küçük veya MAX_DATA sınırını aşıyor
        //   CRC_MISMATCH   — alınan CRC hesaplanan CRC ile eşleşmiyor
        enum class Error : u8 {
            INVALID_LENGTH,
            CRC_MISMATCH,
        };

        using FrameCallback = delegate<void, u8*, u8, u8>;
        using ErrorCallback = delegate<void, Error>;

        struct Slice { u8* data; u8 size; };

        Parser() { reset(); }

        // Doğrudan yazılabilecek bölgeyi döner — zero-copy okuma için.
        Slice write_window() {
            return { buffer + write_pos, static_cast<u8>(BUFFER_SIZE - write_pos) };
        }

        // write_window()'a n bayt yazıldıktan sonra çağrılır; parse eder.
        void commit(u8 n) {
            write_pos += n;
            while (parse_pos < write_pos) {
                advance();
            }
        }

        void set_on_frame_completed(FrameCallback cb) { on_frame_completed = cb; }
        void set_on_error(ErrorCallback cb)           { on_error = cb; }

    private:
        enum class State : u8 {
            HEADER_WAIT,
            LENGTH_WAIT,
            DATA_READING,
            CRC_WAIT
        };

        void advance() {
            u8 byte = buffer[parse_pos];
            switch (state) {

                case State::HEADER_WAIT:
                    if (byte == wireframe::HEADER[header_matched]) {
                        parse_pos++;
                        header_matched++;
                        if (header_matched == wireframe::HEADER_SIZE) {
                            state = State::LENGTH_WAIT;
                        }
                    } else {
                        // Sadece bu baytı tüket; buffer'daki geri kalanlar geçerli.
                        parse_pos++;
                        header_matched = 0;
                        // Hatalı bayt yeni bir header başlangıcı olabilir.
                        if (byte == wireframe::HEADER[0]) {
                            header_matched = 1;
                        }
                    }
                    break;

                case State::LENGTH_WAIT:
                    // DATA en az SEQ(1) + CH_ID(1) = MIN_DATA_LEN byte olmalı
                    if (byte < wireframe::MIN_DATA_LEN || byte > MAX_DATA) {
                        on_error(Error::INVALID_LENGTH);
                        parse_pos++;  // geçersiz length baytını tüket
                        consume();
                        break;
                    }
                    parse_pos++;
                    data_len       = byte;
                    data_remaining = byte;
                    data_start     = parse_pos; // DATA, length byte'ından hemen sonra başlar
                    crc            = 0;
                    state          = State::DATA_READING;
                    break;

                case State::DATA_READING:
                    parse_pos++;
                    crc = wireframe::crc8_update(crc, byte);
                    data_remaining--;
                    if (data_remaining == 0) {
                        state = State::CRC_WAIT;
                    }
                    break;

                case State::CRC_WAIT:
                    parse_pos++;  // CRC baytını tüket
                    if (byte == crc) {
                        if (on_frame_completed.is_valid()) {
                            on_frame_completed(buffer, data_start, data_len);
                        }
                    } else {
                        on_error(Error::CRC_MISMATCH);
                    }
                    consume();
                    break;

                default:
                    consume();
                    break;
            }
        }

        // Frame sınırı geçilince çağrılır.
        // parse_pos'tan write_pos'a kadar olan baytları buffer başına kaydırır;
        // bir sonraki frame için state'i sıfırlar.
        // Bir spin_once'ta birden fazla frame varsa veriler kaybolmaz.
        void consume() {
            u8 remaining = write_pos - parse_pos;
            for (u8 i = 0; i < remaining; i++) {
                buffer[i] = buffer[parse_pos + i];
            }
            write_pos      = remaining;
            parse_pos      = 0;
            header_matched = 0;
            data_start     = 0;
            data_len       = 0;
            data_remaining = 0;
            crc            = 0;
            state          = State::HEADER_WAIT;
        }

        // Sadece constructor'dan çağrılır.
        void reset() {
            parse_pos      = 0;
            write_pos      = 0;
            header_matched = 0;
            data_start     = 0;
            data_len       = 0;
            data_remaining = 0;
            crc            = 0;
            state          = State::HEADER_WAIT;
        }

        FrameCallback on_frame_completed;
        ErrorCallback on_error;

        u8    buffer[BUFFER_SIZE];
        u8    write_pos;       // producer — yeni baytların yazıldığı konum
        u8    parse_pos;       // consumer — parser'ın okumakta olduğu konum
        u8    header_matched;  // eşleşen header bayt sayısı
        u8    data_start;      // DATA bölümünün buffer içindeki başlangıç offseti
        u8    data_len;        // LENGTH alanından gelen toplam DATA uzunluğu
        u8    data_remaining;  // okunmayı bekleyen DATA bayt sayısı
        u8    crc;             // DATA baytları üzerindeki kayan CRC-8 değeri
        State state;
    };

} // namespace minros::core
