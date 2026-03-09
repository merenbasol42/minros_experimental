#pragma once
#include <cstring>
#include "msg_base.hpp"
#include <minros/utils/endian.hpp>

namespace minros::std_msgs {

struct Vector3 : MsgBase<Vector3> {

    // MsgBase'in from_bytes/to_bytes'i private deserialize/serialize'i cagirabilsin
    friend struct MsgBase<Vector3>;

    static constexpr u8  SIZE          = 3u * sizeof(float);  // 12 byte
    static constexpr u8  TYPE_ID       = 0x08;       // MsgTypeId::VECTOR3
    static constexpr u8  FIELD_COUNT   = 3;
    static constexpr char     FIELD_NAMES[] = "x,y,z";
    static constexpr u8  FIELD_TYPES[] = {6, 6, 6};  // FieldType::F32 x3

    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

private:
    // Dogrudan cagrma engellendi — sadece MsgBase::from_bytes uzerinden erisilir
    // Boylece size check her zaman yapilmis olur
    void deserialize(const u8* buf) noexcept {
        x = utils::endian::load_le<float>(buf);
        y = utils::endian::load_le<float>(buf +     sizeof(float));
        z = utils::endian::load_le<float>(buf + 2 * sizeof(float));
    }

    void serialize(u8* buf) const noexcept {
        utils::endian::store_le<float>(buf,                     x);
        utils::endian::store_le<float>(buf +     sizeof(float), y);
        utils::endian::store_le<float>(buf + 2 * sizeof(float), z);
    }
};

static_assert(sizeof(Vector3) == Vector3::SIZE, "Vector3: beklenmedik padding!");

}  // namespace minros::std_msgs
