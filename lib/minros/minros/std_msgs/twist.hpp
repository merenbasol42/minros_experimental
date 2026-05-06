#pragma once
#include <cstring>
#include "msg_base.hpp"
#include "vector3.hpp"

namespace minros::std_msgs {

struct Twist : MsgBase<Twist> {

    friend struct MsgBase<Twist>;

    // linear (12) + angular (12) = 24 byte
    static constexpr u8  SIZE          = 2u * Vector3::SIZE;
    static constexpr u8  TYPE_ID       = 0x0A;                // MsgTypeId::TWIST
    static constexpr u8  FIELD_COUNT   = 6;
    static constexpr char     FIELD_NAMES[] =
        "linear.x,linear.y,linear.z,angular.x,angular.y,angular.z";
    static constexpr u8  FIELD_TYPES[] = {6, 6, 6, 6, 6, 6};  // FieldType::F32 x6

    Vector3 linear;
    Vector3 angular;

private:
    void deserialize(const u8* buf) noexcept {
        (void)linear.from_bytes(buf,                  Vector3::SIZE);
        (void)angular.from_bytes(buf + Vector3::SIZE, Vector3::SIZE);
    }

    void serialize(u8* buf) const noexcept {
        linear.to_bytes(buf);
        angular.to_bytes(buf + Vector3::SIZE);
    }
};

static_assert(sizeof(Twist) == Twist::SIZE, "Twist: beklenmedik padding!");

}  // namespace minros::std_msgs
