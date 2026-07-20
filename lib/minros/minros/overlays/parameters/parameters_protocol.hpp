#pragma once

#include <minros/utils/utils.hpp>


namespace minros::overlays::parameters::protocol {

    // ─────────────────────────────────────────────────────────────────────────
    // minros parameters overlay — wire protokolü
    //
    // Rezerve kanal bloğu (protokol overlay'leri):
    //   249 = reliability ACK
    //   248 = logging
    //   247 = parameters REQ  (host → düğüm : GET, SET)
    //   246 = parameters RES  (düğüm → host : VALUE, ERR)
    //
    // İki kanal, reliability'nin "kanal başına tek publisher" sözleşmesini
    // sağlamak içindir: REQ'te tek publisher host, RES'te tek publisher düğüm.
    //
    // Frame'ler (CH_ID = ilgili kanal, payload):
    //   GET   : [OP=0x01][PARAM_ID]
    //   SET   : [OP=0x02][PARAM_ID][FAMILY_ID][TYPE_ID][msg bytes...]
    //   VALUE : [OP=0x03][PARAM_ID][FAMILY_ID][TYPE_ID][msg bytes...]
    //   ERR   : [OP=0x04][PARAM_ID][CODE]
    //
    // OP baytı, core'un anlamını bilmediği opak bir head önekidir (logging FLAGS
    // gibi). Değer tipi [FAMILY_ID][TYPE_ID] mesaj-tip tanımlayıcısıdır; primitive
    // (Float32 = 0x00 0x00) ve kompozit (PidGains = 0x00 0x0B) mesajları kapsar.
    // Ayrıntı: parameters-protocol.md
    // ─────────────────────────────────────────────────────────────────────────

    constexpr u8 PARAM_REQ_CHANNEL_ID = 247;  // host → düğüm
    constexpr u8 PARAM_RES_CHANNEL_ID = 246;  // düğüm → host

    enum class OpCode : u8 {
        GET   = 0x01,
        SET   = 0x02,
        VALUE = 0x03,
        ERR   = 0x04,
    };

    enum class ErrCode : u8 {
        UNKNOWN_ID    = 0x00,   // bu ID'de kayıtlı parametre yok
        TYPE_MISMATCH = 0x01,   // [FAMILY_ID][TYPE_ID] kayıtlı tiple uyuşmuyor
        READ_ONLY     = 0x02,   // parametre salt-okunur
        BAD_LENGTH    = 0x03,   // msg bytes uzunluğu tipin SIZE'ıyla tutarsız
    };

} // namespace minros::overlays::parameters::protocol
