#pragma once
#include <cstring>
#include "msg_base.hpp"

namespace minros::std_msgs {

struct PidGains : MsgBase<PidGains> {

    friend struct MsgBase<PidGains>;

    // kp (4) + ki (4) + kd (4) = 12 byte
    static constexpr u8  SIZE          = 3u * sizeof(float);
    static constexpr u8  TYPE_ID       = 0x0B;        // MsgTypeId::PID_GAINS
    static constexpr u8  FIELD_COUNT   = 3;
    static constexpr char     FIELD_NAMES[] = "kp,ki,kd";
    static constexpr u8  FIELD_TYPES[] = {6, 6, 6};  // FieldType::F32 x3

    float kp{0.0f};
    float ki{0.0f};
    float kd{0.0f};

private:
    void deserialize(const u8* buf) noexcept {
        kp = utils::endian::load_le<float>(buf);
        ki = utils::endian::load_le<float>(buf +     sizeof(float));
        kd = utils::endian::load_le<float>(buf + 2 * sizeof(float));
    }

    void serialize(u8* buf) const noexcept {
        utils::endian::store_le<float>(buf,                     kp);
        utils::endian::store_le<float>(buf +     sizeof(float), ki);
        utils::endian::store_le<float>(buf + 2 * sizeof(float), kd);
    }
};

static_assert(sizeof(PidGains) == PidGains::SIZE, "PidGains: beklenmedik padding!");

}  // namespace minros::std_msgs
