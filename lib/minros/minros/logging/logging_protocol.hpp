#pragma once

#include <minros/utils/utils.hpp>


namespace minros::logging::protocol {

    // ─────────────────────────────────────────────────────────────────────────
    // minros logging overlay — wire protokolü
    //
    // Rezerve kanal bloğu (protokol overlay'leri):
    //   249 = reliability ACK   (reliability::protocol::ACK_CHANNEL_ID)
    //   248 = logging            (bu dosya)
    // Overlay'lerin kullanıcı kanallarıyla çakışmaması için üstteki blok ayrılmıştır.
    //
    // Log frame'i (CH_ID = LOG_CHANNEL_ID, payload):
    //     PAYLOAD = [FLAGS(1)][text/bytes...]
    //     FLAGS opak bir head önekidir; core anlamını bilmez (reliability seq gibi).
    //
    // FLAGS bit yerleşimi:
    //     bit 0       LAST   : 1 → bu, log'un son (veya tek) parçası
    //     bit 1..3    LEVEL  : seviye (0..4), her parçada taşınır → parse tekdüze
    //     bit 4..7    SEQ4   : 0..15 dönen parça sayacı; kayıp/atlama tespiti
    //
    // Tek frame'e sığan log: LAST=1, SEQ4=0.
    // Parçalı log: her parça SEQ4'ü +1 taşır; sink süreklilik kontrolü yapar.
    // Kanal unreliable (best-effort): parça düşerse sink o log'u atar, bozuk
    // satır üretmez.
    // ─────────────────────────────────────────────────────────────────────────

    constexpr u8 LOG_CHANNEL_ID = 248;

    enum class Level : u8 {
        DEBUG = 0,
        INFO  = 1,
        WARN  = 2,
        ERROR = 3,
        FATAL = 4
    };

    // ── FLAGS pack/unpack ────────────────────────────────────────────────────

    constexpr u8 pack_flags(Level level, u8 seq4, bool last) {
        return static_cast<u8>(
            (static_cast<u8>(seq4 & 0x0F) << 4) |
            ((static_cast<u8>(level) & 0x07) << 1) |
            (last ? 0x01u : 0x00u)
        );
    }

    constexpr bool  flag_last (u8 flags) { return (flags & 0x01) != 0; }
    constexpr Level flag_level(u8 flags) { return static_cast<Level>((flags >> 1) & 0x07); }
    constexpr u8    flag_seq  (u8 flags) { return static_cast<u8>((flags >> 4) & 0x0F); }

} // namespace minros::logging::protocol
