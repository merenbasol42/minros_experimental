#pragma once

#include <minros/utils/utils.hpp>
#include <minros/core/wireframe.hpp>


namespace minros::core {


    enum class ParserState: u8 {
        HEADER_WAIT,
        LENGTH_WAIT,
        DATA_READING,
        CRC_WAIT
    };

    // Parser hata kodları.
    //   INVALID_LENGTH — length alanı MIN_DATA_LEN'den küçük veya MAX_DATA sınırını aşıyor
    //   CRC_MISMATCH   — alınan CRC hesaplanan CRC ile eşleşmiyor
    enum class ParserError : u8 {
        INVALID_LENGTH,
        CRC_MISMATCH,
    };



    using utils::delegate;

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
            wireframe::HEADER_SIZE         +
            1u /* size of length bytes */  +
            MAX_DATA                       +
            1u /* size of cheksum bytes */ ; 

    public:
        // fn(buffer, from, to, obj)
        using FrameCallback = delegate<u8*, u8, u8>;
        // fn (err_code, obj)
        using ErrorCallback = delegate<ParserError>;

    private:
        FrameCallback on_frame_completed;
        ErrorCallback on_error;

        u8  buffer[BUFFER_SIZE];
        u8  frame_index;    // current write position in buffer
        u8  header_count;   // number of matched header bytes
        u8  data_start;     // buffer index where DATA section begins
        u8  data_length;    // total DATA length from LENGTH field
        u8  data_remaining; // DATA bytes still to be read
        u8  checksum;       // running CRC-8 of DATA bytes

        ParserState state;

    public:
        Parser()
            : frame_index(0), header_count(0), data_start(0),
              data_length(0), data_remaining(0), checksum(0),
              state(ParserState::HEADER_WAIT)
        {}

        void process(u8* arr, u8 size) {
            for (u8 i = 0; i < size; i++) {
                process_once(arr[i]);
            }
        }

        void set_on_frame_completed(FrameCallback cb) {
            on_frame_completed = cb;
        }

        // Hata callback'i. ParserError kodu ve void* context ile çağrılır.
        // delegate<ParserError>::Fn = void (*)(ParserError, void*)
        void set_on_error(ErrorCallback cb) {
            on_error = cb;
        }

        void process_once(u8 byte) {
            switch (state) {
    
                case ParserState::HEADER_WAIT:
                    if (byte == wireframe::HEADER[header_count]) {
                        buffer[frame_index++] = byte;
                        header_count++;
                        if (header_count == wireframe::HEADER_SIZE) {
                            state = ParserState::LENGTH_WAIT;
                        }
                    } else {
                        reset();
                        // byte might be the start of a new header
                        if (byte == wireframe::HEADER[0]) {
                            buffer[frame_index++] = byte;
                            header_count = 1;
                        }
                    }
                    break;
    
                case ParserState::LENGTH_WAIT:
                    // DATA en az SEQ(1) + CH_ID(1) = MIN_DATA_LEN byte olmalı
                    if (byte < wireframe::MIN_DATA_LEN || byte > MAX_DATA) {
                        on_error(ParserError::INVALID_LENGTH);
                        reset();
                        break;
                    }
                    buffer[frame_index++] = byte;
                    data_length    = byte;
                    data_remaining = byte;
                    data_start     = frame_index; // DATA begins right after length byte
                    checksum       = 0;
                    state          = ParserState::DATA_READING;
                    break;
    
                case ParserState::DATA_READING:
                    buffer[frame_index++] = byte;
                    checksum = wireframe::crc8_update(checksum, byte);
                    data_remaining--;
                    if (data_remaining == 0) {
                        state = ParserState::CRC_WAIT;
                    }
                    break;
    
                case ParserState::CRC_WAIT:
                    if (byte == checksum) {
                        if (on_frame_completed.is_valid()) {
                            on_frame_completed(buffer, data_start, data_length);
                        }
                    } else {
                        on_error(ParserError::CRC_MISMATCH);
                    }
                    reset();
                    break;
    
                default:
                    reset();
                    break;
            }
        }
        
    private:
        void reset() {
            frame_index  = 0;
            header_count = 0;
            checksum     = 0;
            state        = ParserState::HEADER_WAIT;
        }

    };

} // namespace minros::core
