#pragma once
#include "int.hpp"
#include <cstring>

//
// Wire formatı little-endian'dır.
// Bu başlık, host endianness'ını compile-time'da saptar ve
// store_le / load_le ile şeffaf dönüşüm sağlar.
//
// Little-endian platformlarda (ARM Cortex-M, x86/x64) tüm bswap
// çağrıları if constexpr sayesinde derleme aşamasında elenir → sıfır overhead.
// Big-endian platformlarda swap otomatik uygulanır.
//
// Kullanım:
//   utils::endian::store_le<float>(buf, value);    // host → little-endian buffer
//   float v = utils::endian::load_le<float>(buf);  // little-endian buffer → host
//

namespace minros::utils::endian {

// ── Compile-time endianness tespiti ─────────────────────────────────────────
//
// GCC / Clang: __BYTE_ORDER__ ve __ORDER_BIG_ENDIAN__ önceden tanımlıdır.
// Makro tanımlı değilse (MSVC vb.) little-endian varsayılır; bu durum
// tüm hedef MCU platformlarını (Cortex-M, ESP32, AVR) kapsar.

#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && \
    (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    constexpr bool NATIVE_IS_LITTLE = false;
#else
    constexpr bool NATIVE_IS_LITTLE = true;
#endif

// ── Byte swap yardımcıları ───────────────────────────────────────────────────

namespace detail {

inline u16 bswap(u16 v) noexcept {
    return static_cast<u16>((v >> 8u) | (v << 8u));
}

inline u32 bswap(u32 v) noexcept {
    return ((v & 0xFF000000u) >> 24u)
         | ((v & 0x00FF0000u) >>  8u)
         | ((v & 0x0000FF00u) <<  8u)
         | ((v & 0x000000FFu) << 24u);
}

inline i16 bswap(i16 v) noexcept {
    return static_cast<i16>(bswap(static_cast<u16>(v)));
}

inline i32 bswap(i32 v) noexcept {
    return static_cast<i32>(bswap(static_cast<u32>(v)));
}

// float: bit düzeyinde u32 olarak swap edilir
inline float bswap(float v) noexcept {
    u32 tmp;
    memcpy(&tmp, &v, sizeof(float));
    tmp = bswap(tmp);
    float result;
    memcpy(&result, &tmp, sizeof(float));
    return result;
}

} // namespace detail

// ── store_le / load_le ───────────────────────────────────────────────────────

// host değerini little-endian olarak buf'a yazar
template<typename T>
inline void store_le(u8* buf, T value) noexcept {
    if constexpr (!NATIVE_IS_LITTLE) {
        value = detail::bswap(value);
    }
    memcpy(buf, &value, sizeof(T));
}

// buf'taki little-endian değeri host endianness'ına çevirerek döndürür
template<typename T>
inline T load_le(const u8* buf) noexcept {
    T value;
    memcpy(&value, buf, sizeof(T));
    if constexpr (!NATIVE_IS_LITTLE) {
        value = detail::bswap(value);
    }
    return value;
}

} // namespace minros::utils::endian
