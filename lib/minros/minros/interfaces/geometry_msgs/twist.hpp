#pragma once
#include <cstring>
#include "minros/interfaces/msg_base.hpp"
#include "vector3.hpp"

namespace minros::interfaces::geometry_msgs {

struct Twist : MsgBase<Twist> {

    friend struct MsgBase<Twist>;

    // linear (12) + angular (12) = 24 byte
    static constexpr u8  SIZE      = 2u * Vector3::SIZE;
    static constexpr u8  FAMILY_ID = 0x01;   // geometry_msgs ailesi
    static constexpr u8  TYPE_ID   = 0x02;   // geometry_msgs-yerel: TWIST

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

}  // namespace minros::interfaces::geometry_msgs
