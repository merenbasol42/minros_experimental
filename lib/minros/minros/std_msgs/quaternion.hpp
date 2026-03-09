#pragma once
#include <cstring>
#include "msg_base.hpp"
#include <minros/utils/endian.hpp>

namespace minros::std_msgs {

struct Quaternion : MsgBase<Quaternion> {

    friend struct MsgBase<Quaternion>;

    static constexpr u8  SIZE          = 4u * sizeof(float);  // 16 byte
    static constexpr u8  TYPE_ID       = 0x09;          // MsgTypeId::QUATERNION
    static constexpr u8  FIELD_COUNT   = 4;
    static constexpr char     FIELD_NAMES[] = "x,y,z,w";
    static constexpr u8  FIELD_TYPES[] = {6, 6, 6, 6};  // FieldType::F32 x4

    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float w{1.0f};

private:
    void deserialize(const u8* buf) noexcept {
        x = utils::endian::load_le<float>(buf);
        y = utils::endian::load_le<float>(buf +     sizeof(float));
        z = utils::endian::load_le<float>(buf + 2 * sizeof(float));
        w = utils::endian::load_le<float>(buf + 3 * sizeof(float));
    }

    void serialize(u8* buf) const noexcept {
        utils::endian::store_le<float>(buf,                    x);
        utils::endian::store_le<float>(buf +     sizeof(float), y);
        utils::endian::store_le<float>(buf + 2 * sizeof(float), z);
        utils::endian::store_le<float>(buf + 3 * sizeof(float), w);
    }
};

static_assert(sizeof(Quaternion) == Quaternion::SIZE, "Quaternion: beklenmedik padding!");

}  // namespace minros::std_msgs
