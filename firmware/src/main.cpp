#include <Arduino.h>
#include <cstdio>

#include <minros/raw_node.hpp>
#include <minros/node.hpp>
#include <minros/overlays/reliability/reliable.hpp>
#include <minros/overlays/parameters/params.hpp>
#include <minros/interfaces/geometry_msgs/vector3.hpp>

using minros::u8;
using minros::u32;

namespace param       = minros::overlays::parameters;
namespace reliability = minros::overlays::reliability;

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

// Parametre: echo gain'i (eksen-başına çarpan). Host PARAM_REQ/RES üzerinden
// get/set eder. Varsayılan (2,2,2) → eski ×2 davranışı. Her iki modda da aynı.
static constexpr u8 PARAM_GAIN = 0;

using Vector3 = minros::interfaces::geometry_msgs::Vector3;

// gain — param id 0. Host set ettikçe değeri değişir; echo_of onu okur.
static Vector3 gain;

// Echo dönüşümü: her bileşeni gain ile çarpar. (Sabit ×2 yerine çalışma-anı
// parametresi — Python testleri payload == girdi*gain bekler. Varsayılan gain
// (2,2,2) eski davranışı korur; host set ile değiştirilebilir.)
static Vector3 echo_of(const Vector3& m) {
    Vector3 o;
    o.x = m.x * gain.x;
    o.y = m.y * gain.y;
    o.z = m.z * gain.z;
    return o;
}

// PARAM_GAIN sınırları — BEFORE_SET bu aralığın dışındaki değerleri reddeder
// (örn. host'tan yanlışlıkla gelen aşırı/negatif bir çarpan echo'yu bozmasın).
static constexpr float GAIN_MIN = 0.0f;
static constexpr float GAIN_MAX = 10.0f;

static bool gain_in_range(const Vector3& v) {
    return v.x >= GAIN_MIN && v.x <= GAIN_MAX
        && v.y >= GAIN_MIN && v.y <= GAIN_MAX
        && v.z >= GAIN_MIN && v.z <= GAIN_MAX;
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

// Parametre tablosu artık derleme-zamanı constexpr (flash'ta yaşar); &gain statik
// ömürlü olduğu için adresi geçerli bir constant expression'dır.
static constexpr auto PARAM_TABLE = param::table(
    param::rw<PARAM_GAIN>(&gain));

// Parametre event handler: BEFORE_SET'te sınır dışı gain reddedilir, AFTER_SET'te
// başarılı yazım loglanır (host tanılaması için).
static bool on_param_event(u8 id, param::Event ev,
                            const u8* bytes, u8 len, void*) {
    if (id != PARAM_GAIN) return true;   // yalnızca gain izleniyor

    Vector3 v;
    if (!v.from_bytes(bytes, len)) return false;

    if (ev == param::Event::BEFORE_SET) {
        if (!gain_in_range(v)) {
            char msg[64];
            snprintf(msg, sizeof(msg), "PARAM_GAIN reddedildi: %.2f %.2f %.2f", v.x, v.y, v.z);
            node.log_warn(msg);
            return false;
        }
        return true;
    }

    // AFTER_SET — değer storage'a yazıldı, bildirim amaçlı logla.
    char msg[64];
    snprintf(msg, sizeof(msg), "PARAM_GAIN set: %.2f %.2f %.2f", v.x, v.y, v.z);
    node.log_info(msg);
    return true;
}

void setup() {
    Serial.begin(115200);
    node.transport = serial_transport;

    gain.x = gain.y = gain.z = 2.0f;                  // varsayılan ×2
    node.set_params(PARAM_TABLE);                     // Node facade param sunucusu
    node.set_param_event_handler({&on_param_event, nullptr});

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

static minros::RawNode<>     node;
static reliability::Reliable rel{ node };   // aynı node'a takılır (ACK kanalına abone)

// Parametre tablosu artık derleme-zamanı constexpr (flash'ta yaşar); &gain statik
// ömürlü olduğu için adresi geçerli bir constant expression'dır.
static constexpr auto PARAM_TABLE = param::table(
    param::rw<PARAM_GAIN>(&gain));
static param::Params     params{ node, PARAM_TABLE };  // PARAM_REQ'e abone, tablo CTAD ile bağlı

// Parametre logları için zero-buffer publisher (Node<> facade'ının log_* metodlarının
// RawNode karşılığı — bkz. overlays::logging::Logger).
static minros::overlays::logging::Logger<decltype(node)> plog{ node };

// Parametre event handler: BEFORE_SET'te sınır dışı gain reddedilir, AFTER_SET'te
// başarılı yazım loglanır (host tanılaması için).
static bool on_param_event(u8 id, param::Event ev,
                            const u8* bytes, u8 len, void*) {
    if (id != PARAM_GAIN) return true;   // yalnızca gain izleniyor

    Vector3 v;
    if (!v.from_bytes(bytes, len)) return false;

    if (ev == param::Event::BEFORE_SET) {
        if (!gain_in_range(v)) {
            char msg[64];
            snprintf(msg, sizeof(msg), "PARAM_GAIN reddedildi: %.2f %.2f %.2f", v.x, v.y, v.z);
            plog.warn(msg);
            return false;
        }
        return true;
    }

    // AFTER_SET — değer storage'a yazıldı, bildirim amaçlı logla.
    char msg[64];
    snprintf(msg, sizeof(msg), "PARAM_GAIN set: %.2f %.2f %.2f", v.x, v.y, v.z);
    plog.info(msg);
    return true;
}

// Reliable publisher buffer'ı ACK gelene kadar SABİT kalmalı (Reliable pointer tutar).
static u8 rel_tx[Vector3::SIZE];

// Unreliable: CH 0 → CH 1
static void on_unrel_bytes(u8* payload, u8 len, void*) {
    Vector3 in;
    if (!in.from_bytes(payload, len)) return;
    Vector3 out = echo_of(in);

    u8 buf[Vector3::SIZE];
    minros::interfaces::serialize_to(out, buf);
    node.publish(CH_UNREL_PUB, buf, Vector3::SIZE);
}

// Reliable: CH 2 → CH 3 (callback seq önekı ayıklanmış payload alır)
static void on_rel_bytes(u8* payload, u8 len, void*) {
    Vector3 in;
    if (!in.from_bytes(payload, len)) return;
    Vector3 out = echo_of(in);

    if (rel.can_send(CH_REL_PUB)) {                 // önceki echo ACK'lendi mi?
        minros::interfaces::serialize_to(out, rel_tx);
        rel.publish(CH_REL_PUB, rel_tx, Vector3::SIZE);
    }
}

void setup() {
    Serial.begin(115200);
    node.transport = serial_transport;

    gain.x = gain.y = gain.z = 2.0f;                  // varsayılan ×2 (tablo zaten &gain'e bağlı)
    params.set_event_handler({&on_param_event, nullptr});

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