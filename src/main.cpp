#include <Arduino.h>

#include <minros/raw_node.hpp>
#include <minros/node.hpp>
#include <minros/std_msgs/vector3.hpp>

#define ENABLE_RAW_NODE    1
#define ENABLE_HL_NODE     1
#define ENABLE_HL_REL_NODE 0


// ─── Transport ────────────────────────────────────────────────────────────────

static u8   tp_get_size   (void*)               { return static_cast<u8>(Serial.available()); }
static u32  tp_get_time   (void*)               { return millis(); }
static void tp_read_bytes (u8* b, u8 n, void*)  { Serial.readBytes(b, n); }
static void tp_write_bytes(u8* b, u8 n, void*)  { Serial.write(b, n); }

static minros::Transport serial_transport = {
    .send_bytes = { tp_write_bytes, nullptr },
    .read_bytes = { tp_read_bytes,  nullptr },
    .get_size   = { tp_get_size,    nullptr },
    .get_time   = { tp_get_time,    nullptr },
};


// ─── Düşük seviye: ch 0x00 → ch 0x01 ────────────────────────────────────────

#if ENABLE_RAW_NODE

static minros::RawNode<> raw_node;

static void on_raw_bytes(u8* payload, u8 /*len*/, void*) {
    auto a = (minros::std_msgs::Vector3*) payload;

    minros::std_msgs::Vector3 v3;
    v3.x = a->x * 2.0f;
    v3.y = a->y * 2.0f;
    v3.z = a->z * 2.0f;

    u8 buffer[minros::std_msgs::Vector3::SIZE];
    minros::std_msgs::serialize_to(v3, buffer);
    raw_node.publish(0x01, buffer, minros::std_msgs::Vector3::SIZE);
}

#endif


// ─── Yüksek seviye: ch 0x02 → ch 0x03 ───────────────────────────────────────

#if ENABLE_HL_NODE

static minros::Node<> hl_node;
static minros::Node<>::Publisher<minros::std_msgs::Vector3> hl_pub;

static void on_vector3(const minros::std_msgs::Vector3& msg, void*) {
    minros::std_msgs::Vector3 out;
    out.x = msg.x * 2.0f;
    out.y = msg.y * 2.0f;
    out.z = msg.z * 2.0f;
    hl_pub.publish(out);
}

#endif


// ─── Yüksek seviye + reliable: ch 0x04 → ch 0x05 ────────────────────────────


#if ENABLE_HL_REL_NODE

static minros::Node<> rel_node;
static minros::Node<>::Publisher<minros::std_msgs::Vector3> rel_pub;

// Retransmit otonomdur (Publisher buffer'ı kendi tutar); callback gerekmez.
// can_send false ise önceki mesaj hâlâ uçuşta — bu turu atlarız.
static void on_vector3_rel(const minros::std_msgs::Vector3& msg, void*) {
    minros::std_msgs::Vector3 out;
    out.x = msg.x * 2.0f;
    out.y = msg.y * 2.0f;
    out.z = msg.z * 2.0f;
    rel_pub.publish(out);
}

#endif


// ─── Arduino giriş noktaları ──────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);

#if ENABLE_RAW_NODE
    raw_node.transport = serial_transport;
    raw_node.subscribe(0x00, { on_raw_bytes, nullptr });
#endif

#if ENABLE_HL_NODE
    hl_node.transport = serial_transport;
    hl_pub = hl_node.create_publisher<minros::std_msgs::Vector3>(0x03);
    hl_node.create_subscription<minros::std_msgs::Vector3>(0x02, { on_vector3, nullptr });
#endif

#if ENABLE_HL_REL_NODE
    rel_node.transport = serial_transport;
    rel_pub = rel_node.create_publisher<minros::std_msgs::Vector3>(0x05, /*reliable=*/true);
    rel_node.create_subscription<minros::std_msgs::Vector3>(0x04, { on_vector3_rel, nullptr }, true);
#endif
}

void loop() {
#if ENABLE_RAW_NODE
    raw_node.spin_once();
#endif
#if ENABLE_HL_NODE
    hl_node.spin_once();
#endif
#if ENABLE_HL_REL_NODE
    rel_node.spin_once();
#endif
}
