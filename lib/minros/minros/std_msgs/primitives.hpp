#pragma once
#include <cstring>
#include "msg_base.hpp"

namespace minros::std_msgs {

// ── Float32 ─────────────────────────────────────────────── 4 byte ──

struct Float32 : MsgBase<Float32> {
    friend struct MsgBase<Float32>;
    static constexpr u8  SIZE          = sizeof(float);
    static constexpr u8  TYPE_ID       = 0x00;    // MsgTypeId::FLOAT32
    static constexpr u8  FIELD_COUNT   = 1;
    static constexpr char     FIELD_NAMES[] = "value";
    static constexpr u8  FIELD_TYPES[] = {6};    // FieldType::F32
    float value{0.0f};
private:
    void deserialize(const u8* buf) noexcept { value = utils::endian::load_le<float>(buf); }
    void serialize(u8* buf)   const noexcept { utils::endian::store_le<float>(buf, value); }
};

// ── Int32 ────────────────────────────────────────────────── 4 byte ──

struct Int32 : MsgBase<Int32> {
    friend struct MsgBase<Int32>;
    static constexpr u8  SIZE          = sizeof(i32);
    static constexpr u8  TYPE_ID       = 0x01;    // MsgTypeId::INT32
    static constexpr u8  FIELD_COUNT   = 1;
    static constexpr char     FIELD_NAMES[] = "value";
    static constexpr u8  FIELD_TYPES[] = {5};    // FieldType::I32
    i32 value{0};
private:
    void deserialize(const u8* buf) noexcept { value = utils::endian::load_le<i32>(buf); }
    void serialize(u8* buf)   const noexcept { utils::endian::store_le<i32>(buf, value); }
};

// ── Int16 ────────────────────────────────────────────────── 2 byte ──

struct Int16 : MsgBase<Int16> {
    friend struct MsgBase<Int16>;
    static constexpr u8  SIZE          = sizeof(i16);
    static constexpr u8  TYPE_ID       = 0x02;    // MsgTypeId::INT16
    static constexpr u8  FIELD_COUNT   = 1;
    static constexpr char     FIELD_NAMES[] = "value";
    static constexpr u8  FIELD_TYPES[] = {4};    // FieldType::I16
    i16 value{0};
private:
    void deserialize(const u8* buf) noexcept { value = utils::endian::load_le<i16>(buf); }
    void serialize(u8* buf)   const noexcept { utils::endian::store_le<i16>(buf, value); }
};

// ── UInt32 ───────────────────────────────────────────────── 4 byte ──

struct UInt32 : MsgBase<UInt32> {
    friend struct MsgBase<UInt32>;
    static constexpr u8  SIZE          = sizeof(u32);
    static constexpr u8  TYPE_ID       = 0x04;    // MsgTypeId::UINT32
    static constexpr u8  FIELD_COUNT   = 1;
    static constexpr char     FIELD_NAMES[] = "value";
    static constexpr u8  FIELD_TYPES[] = {2};    // FieldType::U32
    u32 value{0};
private:
    void deserialize(const u8* buf) noexcept { value = utils::endian::load_le<u32>(buf); }
    void serialize(u8* buf)   const noexcept { utils::endian::store_le<u32>(buf, value); }
};

// ── UInt16 ───────────────────────────────────────────────── 2 byte ──

struct UInt16 : MsgBase<UInt16> {
    friend struct MsgBase<UInt16>;
    static constexpr u8  SIZE          = sizeof(u16);
    static constexpr u8  TYPE_ID       = 0x05;    // MsgTypeId::UINT16
    static constexpr u8  FIELD_COUNT   = 1;
    static constexpr char     FIELD_NAMES[] = "value";
    static constexpr u8  FIELD_TYPES[] = {1};    // FieldType::U16
    u16 value{0};
private:
    void deserialize(const u8* buf) noexcept { value = utils::endian::load_le<u16>(buf); }
    void serialize(u8* buf)   const noexcept { utils::endian::store_le<u16>(buf, value); }
};

// ── Int8 ─────────────────────────────────────────────────── 1 byte ──

struct Int8 : MsgBase<Int8> {
    friend struct MsgBase<Int8>;
    static constexpr u8  SIZE          = sizeof(i8);
    static constexpr u8  TYPE_ID       = 0x03;    // MsgTypeId::INT8
    static constexpr u8  FIELD_COUNT   = 1;
    static constexpr char     FIELD_NAMES[] = "value";
    static constexpr u8  FIELD_TYPES[] = {3};    // FieldType::I8
    i8 value{0};
private:
    void deserialize(const u8* buf) noexcept { value = static_cast<i8>(buf[0]); }
    void serialize(u8* buf)   const noexcept { buf[0] = static_cast<u8>(value); }
};

// ── UInt8 ────────────────────────────────────────────────── 1 byte ──

struct UInt8 : MsgBase<UInt8> {
    friend struct MsgBase<UInt8>;
    static constexpr u8  SIZE          = sizeof(u8);
    static constexpr u8  TYPE_ID       = 0x06;    // MsgTypeId::UINT8
    static constexpr u8  FIELD_COUNT   = 1;
    static constexpr char     FIELD_NAMES[] = "value";
    static constexpr u8  FIELD_TYPES[] = {0};    // FieldType::U8
    u8 value{0};
private:
    void deserialize(const u8* buf) noexcept { value = buf[0]; }
    void serialize(u8* buf)   const noexcept { buf[0] = value; }
};

// ── Bool ─────────────────────────────────────────────────── 1 byte ──

struct Bool : MsgBase<Bool> {
    friend struct MsgBase<Bool>;
    static constexpr u8  SIZE          = sizeof(u8);
    static constexpr u8  TYPE_ID       = 0x07;    // MsgTypeId::BOOL
    static constexpr u8  FIELD_COUNT   = 1;
    static constexpr char     FIELD_NAMES[] = "value";
    static constexpr u8  FIELD_TYPES[] = {8};    // FieldType::BOOL
    bool value{false};
private:
    void deserialize(const u8* buf) noexcept { value = buf[0] != 0; }
    void serialize(u8* buf)   const noexcept { buf[0] = value ? 1u : 0u; }
};

// ── static_assert'ler ────────────────────────────────────────────────

static_assert(sizeof(Float32) == Float32::SIZE, "Float32: beklenmedik padding!");
static_assert(sizeof(Int32)   == Int32::SIZE,   "Int32: beklenmedik padding!");
static_assert(sizeof(Int16)   == Int16::SIZE,   "Int16: beklenmedik padding!");
static_assert(sizeof(Int8)    == Int8::SIZE,    "Int8: beklenmedik padding!");
static_assert(sizeof(UInt32)  == UInt32::SIZE,  "UInt32: beklenmedik padding!");
static_assert(sizeof(UInt16)  == UInt16::SIZE,  "UInt16: beklenmedik padding!");
static_assert(sizeof(UInt8)   == UInt8::SIZE,   "UInt8: beklenmedik padding!");
static_assert(sizeof(Bool)    == Bool::SIZE,    "Bool: beklenmedik padding!");

}  // namespace minros::std_msgs
