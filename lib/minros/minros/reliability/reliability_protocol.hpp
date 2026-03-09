#pragma once

#include <minros/utils/int.hpp>



namespace minros::reliability::protocol {
   
    enum class ResponseType: u8 {
        ACK = 0x06
    };
    
    /*
    Mesaj Tanımı {
        RESP  : 1 byte = '0x06' (response tipi, ASCII ACK) 
        CH_ID : 1 byte (Kanal ID'si) 
        SEQ   : 1 byte (Mesajın sequence numarası)

        RESP alanı neden var ?
        aslında zaten sadece ACK gönderiliyor NACK yok ama belki ilerleyen zamanlarda eklenir diye bunun için 
    }
    */

    constexpr u8 ACK_CHANNEL_ID = 249;

} // namespace reliability
