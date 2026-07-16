#pragma once
#include "minros/utils/utils.hpp"

namespace minros::interfaces {

//
// CRTP (Curiously Recurring Template Pattern) temel sinifi
//
// Virtual vs CRTP karsilastirma:
//
//   [Virtual]                       [CRTP]
//   Her object'ta +8 byte (vptr)    +0 byte
//   Indirect call (cache miss)      Direkt call (inlineable)
//   Runtime'da cozumlenir           Derleme zamaninda cozumlenir
//   Derleyici inline edemez         Derleyici tamamen optimize eder
//
// Kullanim: struct Verim : MsgBase<Verim> { ... };
//
template<typename Derived>
struct MsgBase {

    // Ortak size check burada — her derived sinifta tekrarlanmaz.
    // Not: serialize/deserialize, endian::store_le / load_le üzerinden çalışır;
    // wire formatı little-endian'dır, host dönüşümü otomatiktir. Bkz. endian.hpp.
    [[nodiscard]] bool from_bytes(const u8* buf, u8 len) noexcept {
        if (len < Derived::SIZE) return false;
        // static_cast: virtual dispatch degil, derleme zamaninda bagli
        static_cast<Derived*>(this)->deserialize(buf);
        return true;
    }

    void to_bytes(u8* buf) const noexcept {
        static_cast<const Derived*>(this)->serialize(buf);
    }



    // Yardimci: mesaj boyutunu tip uzerinden sorgula
    static constexpr u8 size() noexcept { return Derived::SIZE; }

    // Mesaj tipi kimliği iki parçalıdır: [FAMILY_ID][TYPE_ID].
    //   family_id() — mesaj ailesi (paket)
    //   type_id()   — aile içindeki mesaj kimliği (aile-yerel)
    //
    // FAMILY_ID açık bir kayıt uzayıdır (kapalı enum değil). Numaralandırma
    // aralık şemasıyla yönetilir; yeni aile eklemek çekirdek koda dokunmaz:
    //   0x00–0x7F  resmi / rezerve aileler — proje tahsis eder
    //              (std_msgs = 0x00, geometry_msgs = 0x01, sensor_msgs = 0x02, ...)
    //   0x80–0xFF  özel kullanım (private) — herkes koordinasyonsuz kullanır;
    //              resmi aileler bu bloğu ASLA almaz → çakışma garantili yok.
    static constexpr u8 family_id() noexcept { return Derived::FAMILY_ID; }
    static constexpr u8 type_id()   noexcept { return Derived::TYPE_ID; }
};

//
// Generic yardimci fonksiyonlar
// Yeni mesaj tipi eklendiginde bunlar degismez — template otomatik genisler
//
template<typename Derived>
void serialize_to(const MsgBase<Derived>& msg, u8* buf) noexcept {
    msg.to_bytes(buf);
}

template<typename Derived>
[[nodiscard]] bool deserialize_from(MsgBase<Derived>& msg,
                                    const u8* buf, u8 len) noexcept {
    return msg.from_bytes(buf, len);
}

}  // namespace minros::interfaces
