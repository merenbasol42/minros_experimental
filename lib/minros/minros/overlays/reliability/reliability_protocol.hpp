#pragma once

#include <minros/utils/utils.hpp>



namespace minros::overlays::reliability::protocol {
   
    enum class ResponseType: u8 {
        ACK = 0x06
    };
    
    /*
    Reliable veri frame'i (CH_ID = kullanıcı kanalı):
        PAYLOAD = [SEQ(1)][user bytes...]
        SEQ, Reliable katmanının payload'a koyduğu opak önek; core bunu bilmez.

    ACK frame'i (CH_ID = ACK_CHANNEL_ID, payload):
        RESP  : 1 byte = 0x06 (response tipi, ASCII ACK)
        CH_ID : 1 byte (ACK'lenen kanal)
        SEQ   : 1 byte (ACK'lenen sequence numarası)

        RESP alanı neden var ?
        şu an sadece ACK var, NACK yok; ileride eklenebilsin diye ayrıldı.
    */

    constexpr u8 ACK_CHANNEL_ID = 249;

} // namespace minros::overlays::reliability::protocol
