#pragma once
#include "minros/utils/int.hpp"

namespace minros::std_msgs {

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

    // Mesaj tanimı: "alan:tur,alan:tur" formatında, her derived sinif doldurur
    static constexpr const char* definition() noexcept { return Derived::DEFINITION; }

    // Mesaj tipi adı: "Vector3", "Twist" vb., her derived sinif doldurur
    static constexpr const char* type_name() noexcept { return Derived::TYPE_NAME; }

    // Introduce protokolü erişicileri (intro_protocol::MsgTypeId / FieldType)
    static constexpr u8         type_id()     noexcept { return Derived::TYPE_ID; }
    static constexpr u8         field_count() noexcept { return Derived::FIELD_COUNT; }
    static constexpr const char*     field_names() noexcept { return Derived::FIELD_NAMES; }
    static constexpr const u8*  field_types() noexcept { return Derived::FIELD_TYPES; }
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

}  // namespace minros::std_msgs
