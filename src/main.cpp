#include <Arduino.h>

#include <minros/raw_node.hpp>
#include <minros/node.hpp>
#include <minros/reliability/reliable.hpp>
#include <minros/std_msgs/vector3.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// Düğüm tipi seçimi — YALNIZCA birini seç.
//
//   NODE_TYPE_HL   : tipli yüksek seviye API (minros::Node)
//   NODE_TYPE_RAW  : ham byte API (minros::RawNode) + reliability::Reliable overlay
//
// İki mod da DIŞARIDAN birebir aynı davranır (aynı 4 kanal, aynı Vector3 echo);
// yalnızca içte kullanılan API katmanı değişir. Böylece aynı Python test
// betikleri hem ham hem tipli yolu doğrular.
// ─────────────────────────────────────────────────────────────────────────────
#define NODE_TYPE_HL   0
#define NODE_TYPE_RAW  1

#define NODE_TYPE  NODE_TYPE_HL    // ← düğüm tipini buradan değiştir


// ─── Kanal şeması (cihaz perspektifi) ────────────────────────────────────────
//   CH 0: unreliable sub  (PC publish → cihaz alır)
//   CH 1: unreliable pub  (cihaz echo → PC alır)
//   CH 2: reliable   sub  (PC reliable publish → cihaz alır)
//   CH 3: reliable   pub  (cihaz reliable echo → PC alır)
static constexpr u8 CH_UNREL_SUB = 0;
static constexpr u8 CH_UNREL_PUB = 1;
static constexpr u8 CH_REL_SUB   = 2;
static constexpr u8 CH_REL_PUB   = 3;

using Vector3 = minros::std_msgs::Vector3;

// Echo dönüşümü: her bileşeni 2 ile çarpar. (×2, kimlik echo yerine, gidiş-dönüş
// veri bütünlüğünü doğrulamak için — Python testleri payload == girdi*2 bekler.
// Saf echo isteniyorsa çarpanı kaldır.)
static Vector3 echo_of(const Vector3& m) {
    Vector3 o;
    o.x = m.x * 2.0f;
    o.y = m.y * 2.0f;
    o.z = m.z * 2.0f;
    return o;
}


// ─── Transport (tek Serial, tek düğüm) ───────────────────────────────────────

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


// ═════════════════════════════════════════════════════════════════════════════
//  NODE_TYPE_HL — tipli Node ile echo
// ═════════════════════════════════════════════════════════════════════════════
#if NODE_TYPE == NODE_TYPE_HL

static minros::Node<>                       node;
static minros::Node<>::Publisher<Vector3>   unrel_pub;   // CH 1
static minros::Node<>::Publisher<Vector3>   rel_pub;     // CH 3

static void on_unrel(const Vector3& msg, void*) {
    unrel_pub.publish(echo_of(msg));               // best-effort echo
}

static void on_rel(const Vector3& msg, void*) {
    rel_pub.publish(echo_of(msg));                 // reliable echo (uçuştaysa drop'lanır)
}

void setup() {
    Serial.begin(115200);
    node.transport = serial_transport;

    unrel_pub = node.create_publisher<Vector3>(CH_UNREL_PUB);
    rel_pub   = node.create_publisher<Vector3>(CH_REL_PUB, /*reliable=*/true);

    node.create_subscription<Vector3>(CH_UNREL_SUB, { on_unrel, nullptr });
    node.create_subscription<Vector3>(CH_REL_SUB,   { on_rel,   nullptr }, /*reliable=*/true);
}

void loop() {
    node.spin_once();   // parser + reliable tick birlikte
}


// ═════════════════════════════════════════════════════════════════════════════
//  NODE_TYPE_RAW — ham RawNode + reliability::Reliable overlay ile echo
// ═════════════════════════════════════════════════════════════════════════════
#elif NODE_TYPE == NODE_TYPE_RAW

static minros::RawNode<>                 node;
static minros::reliability::Reliable  rel{ node };   // aynı node'a takılır (ACK kanalına abone)

// Reliable publisher buffer'ı ACK gelene kadar SABİT kalmalı (Reliable pointer tutar).
static u8 rel_tx[Vector3::SIZE];

// Unreliable: CH 0 → CH 1
static void on_unrel_bytes(u8* payload, u8 len, void*) {
    Vector3 in;
    if (!in.from_bytes(payload, len)) return;
    Vector3 out = echo_of(in);

    u8 buf[Vector3::SIZE];
    minros::std_msgs::serialize_to(out, buf);
    node.publish(CH_UNREL_PUB, buf, Vector3::SIZE);
}

// Reliable: CH 2 → CH 3 (callback seq önekı ayıklanmış payload alır)
static void on_rel_bytes(u8* payload, u8 len, void*) {
    Vector3 in;
    if (!in.from_bytes(payload, len)) return;
    Vector3 out = echo_of(in);

    if (rel.can_send(CH_REL_PUB)) {                 // önceki echo ACK'lendi mi?
        minros::std_msgs::serialize_to(out, rel_tx);
        rel.publish(CH_REL_PUB, rel_tx, Vector3::SIZE);
    }
}

void setup() {
    Serial.begin(115200);
    node.transport = serial_transport;

    node.subscribe(CH_UNREL_SUB, { on_unrel_bytes, nullptr });   // best-effort
    rel.subscribe(CH_REL_SUB,    { on_rel_bytes,   nullptr });   // reliable (dedup + ACK)
    rel.register_pub(CH_REL_PUB);
}

void loop() {
    node.spin_once();   // gelen baytlar
    rel.tick();         // timeout / retransmit
}

#else
#error "NODE_TYPE: NODE_TYPE_HL veya NODE_TYPE_RAW olmalı"
#endif
