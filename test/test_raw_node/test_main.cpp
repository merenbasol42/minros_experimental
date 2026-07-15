// ─── minros düşük seviye RawNode + Reliable testi ────────────────────────────────
//
// Strateji: İki RawNode<> arasına BytePipe loopback transport bağlanır.
//   node_a  --[a_to_b]--> node_b   (mesaj)
//   node_b  --[b_to_a]--> node_a   (ACK)
//
// Reliability artık RawNode'un içinde değil; RawNode'a takılan reliability::Reliable
// overlay'idir. seq payload önekinde taşınır, retransmit pointer-tutma ile
// otonomdur (retransmit callback yok).
//
// Test 1 — Unreliable: publish → spin_once → raw callback çağrıldı mı?
// Test 2 — Reliable:   publish → spin_once (callback+ACK) → spin_once (ACK alındı)
// Test 3 — Timeout:    ACK gelmez → tick → tel üzerinde retransmit frame'i çıktı mı?
// Test 4 — Duplicate:  aynı seq iki kez gelirse callback bir kez çağrılmalı
// ─────────────────────────────────────────────────────────────────────────────

#include <unity.h>
#include <cstdint>
#include <cstring>

#include <minros/raw_node.hpp>
#include <minros/overlays/reliability/reliable.hpp>

// ─── BytePipe: sabit boyutlu döngüsel tampon ──────────────────────────────────
struct BytePipe {
    static constexpr uint16_t CAP = 512;
    uint8_t  buf[CAP] = {};
    uint16_t head     = 0;
    uint16_t tail     = 0;

    uint8_t available() const {
        uint16_t n = tail - head;
        return n > 255 ? 255 : static_cast<uint8_t>(n);
    }

    void write(const uint8_t* src, uint8_t len) {
        for (uint8_t i = 0; i < len; i++)
            buf[tail++ % CAP] = src[i];
    }

    void read(uint8_t* dst, uint8_t len) {
        for (uint8_t i = 0; i < len; i++)
            dst[i] = buf[head++ % CAP];
    }
};

// ─── Sahte saat ───────────────────────────────────────────────────────────────
static uint32_t fake_ms = 0;
static uint32_t get_fake_time(void*) { return fake_ms; }

// ─── Transport callback'leri ──────────────────────────────────────────────────
static void    pipe_send (uint8_t* b, uint8_t n, void* ctx) { static_cast<BytePipe*>(ctx)->write(b, n); }
static void    pipe_recv (uint8_t* b, uint8_t n, void* ctx) { static_cast<BytePipe*>(ctx)->read(b, n);  }
static uint8_t pipe_avail(void* ctx)                        { return static_cast<BytePipe*>(ctx)->available(); }

// ─── Yardımcı: iki node'u çift yönlü bağla ───────────────────────────────────
static void connect(minros::RawNode<>& a, minros::RawNode<>& b,
                    BytePipe& a_to_b, BytePipe& b_to_a)
{
    a.transport = {
        .send_bytes = { pipe_send,  &a_to_b },
        .read_bytes = { pipe_recv,  &b_to_a },
        .get_size   = { pipe_avail, &b_to_a },
        .get_time   = { get_fake_time, nullptr },
    };
    b.transport = {
        .send_bytes = { pipe_send,  &b_to_a },
        .read_bytes = { pipe_recv,  &a_to_b },
        .get_size   = { pipe_avail, &a_to_b },
        .get_time   = { get_fake_time, nullptr },
    };
}

void setUp()    { fake_ms = 0; }
void tearDown() {}

// ─────────────────────────────────────────────────────────────────────────────
// Test 1 — Unreliable publish/subscribe: ham byte alışverişi
// ─────────────────────────────────────────────────────────────────────────────

static int     t1_call_count  = 0;
static uint8_t t1_payload[16] = {};
static uint8_t t1_len         = 0;

static void t1_on_bytes(uint8_t* payload, uint8_t len, void*)
{
    t1_call_count++;
    t1_len = len < sizeof(t1_payload) ? len : sizeof(t1_payload);
    memcpy(t1_payload, payload, t1_len);
}

void test_unreliable_raw_bytes()
{
    t1_call_count = 0;
    memset(t1_payload, 0, sizeof(t1_payload));
    t1_len = 0;

    static BytePipe ab, ba;
    ab = {}; ba = {};

    minros::RawNode<> node_a, node_b;
    connect(node_a, node_b, ab, ba);

    node_b.subscribe(0x01, { t1_on_bytes, nullptr });

    const uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    TEST_ASSERT_TRUE_MESSAGE(node_a.publish(0x01, data, sizeof(data)), "publish() false döndü");

    node_b.spin_once();

    TEST_ASSERT_EQUAL_MESSAGE(1, t1_call_count, "callback bir kez çağrılmalıydı");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(sizeof(data), t1_len, "payload uzunluğu yanlış");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data, t1_payload, sizeof(data), "payload içeriği yanlış");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2 — Reliable: publish → callback + ACK → can_send tekrar true
// ─────────────────────────────────────────────────────────────────────────────

static bool    t2_received   = false;
static uint8_t t2_payload[4] = {};

static void t2_on_bytes(uint8_t* payload, uint8_t len, void*)
{
    t2_received = true;
    if (len >= 4) memcpy(t2_payload, payload, 4);
}

void test_reliable_ack_roundtrip()
{
    t2_received = false;
    memset(t2_payload, 0, sizeof(t2_payload));

    static BytePipe ab, ba;
    ab = {}; ba = {};

    minros::RawNode<> node_a, node_b;
    connect(node_a, node_b, ab, ba);

    minros::overlays::reliability::Reliable rel_a{node_a};   // publisher tarafı (ACK alır)
    minros::overlays::reliability::Reliable rel_b{node_b};   // subscriber tarafı (ACK yollar)

    rel_b.subscribe(0x02, { t2_on_bytes, nullptr });

    const uint8_t data[] = {0x01, 0x02, 0x03, 0x04};   // ACK'e kadar yaşamalı
    TEST_ASSERT_TRUE_MESSAGE(rel_a.can_send(0x02), "başta can_send false");
    TEST_ASSERT_TRUE_MESSAGE(rel_a.publish(0x02, data, sizeof(data)), "publish() false döndü");
    TEST_ASSERT_FALSE_MESSAGE(rel_a.can_send(0x02), "ACK bekleniyorken can_send true");

    node_b.spin_once();  // mesajı işle → callback + ACK gönder
    node_a.spin_once();  // ACK'i al    → ack_pending temizlendi

    TEST_ASSERT_TRUE_MESSAGE(t2_received, "subscriber callback çağrılmadı");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data, t2_payload, sizeof(data), "payload içeriği yanlış");
    TEST_ASSERT_TRUE_MESSAGE(rel_a.can_send(0x02), "ACK sonrası can_send hâlâ false");

    // ACK alındıktan sonra yeni mesaj gönderilebilmeli
    const uint8_t data2[] = {0x0A, 0x0B, 0x0C, 0x0D};
    TEST_ASSERT_TRUE_MESSAGE(rel_a.publish(0x02, data2, sizeof(data2)),
                             "ikinci publish() false döndü");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3 — Timeout: ACK gelmez → tick → tel üzerinde retransmit frame'i çıkmalı
//
// node_a'nın okuma tarafı kapalı (always_zero) → ACK node_a'ya ulaşmaz, timeout
// gerçekten tetiklenir. Retransmit'in kanıtı: ab pipe'ında yeni frame belirir.
// ─────────────────────────────────────────────────────────────────────────────

static int t3_call_count = 0;
static void t3_on_bytes(uint8_t*, uint8_t, void*) { t3_call_count++; }

static uint8_t always_zero(void*)                 { return 0; }
static void    noop_recv(uint8_t*, uint8_t, void*) {}

void test_reliable_timeout_retransmit()
{
    t3_call_count = 0;
    fake_ms       = 0;

    static BytePipe ab;     // node_a → node_b (mesaj)
    static BytePipe sink;   // node_b → sink   (ACK; node_a okumaz)
    ab = {}; sink = {};

    minros::RawNode<> node_a, node_b;
    node_a.transport = {
        .send_bytes = { pipe_send,   &ab },
        .read_bytes = { noop_recv,   nullptr },   // hiç okumaz
        .get_size   = { always_zero, nullptr },   // her zaman 0
        .get_time   = { get_fake_time, nullptr },
    };
    node_b.transport = {
        .send_bytes = { pipe_send,  &sink },
        .read_bytes = { pipe_recv,  &ab   },
        .get_size   = { pipe_avail, &ab   },
        .get_time   = { get_fake_time, nullptr },
    };

    minros::overlays::reliability::Reliable rel_a{node_a};
    minros::overlays::reliability::Reliable rel_b{node_b};
    rel_b.subscribe(0x03, { t3_on_bytes, nullptr });

    const uint8_t data[] = {0xFF};
    TEST_ASSERT_TRUE(rel_a.publish(0x03, data, sizeof(data)));

    node_b.spin_once();                       // ilk mesajı işle → callback + ACK (sink'e)
    TEST_ASSERT_EQUAL_MESSAGE(1, t3_call_count, "ilk mesajda callback çağrılmadı");
    TEST_ASSERT_EQUAL_MESSAGE(0, ab.available(), "ab boşalmalıydı");

    // Timeout öncesi tick — retransmit olmamalı
    fake_ms = 30;
    node_a.spin_once();
    rel_a.tick();
    TEST_ASSERT_EQUAL_MESSAGE(0, ab.available(), "timeout öncesi retransmit oldu");

    // Timeout sonrası tick — retransmit frame'i ab'ye yazılmalı
    fake_ms = 60;
    node_a.spin_once();
    rel_a.tick();
    TEST_ASSERT_TRUE_MESSAGE(ab.available() > 0, "timeout sonrası retransmit olmadı");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4 — Duplicate filtreleme: aynı seq tekrar gelirse callback çağrılmaz
// ─────────────────────────────────────────────────────────────────────────────

static int t4_call_count = 0;
static void t4_on_bytes(uint8_t*, uint8_t, void*) { t4_call_count++; }

void test_reliable_duplicate_filtering()
{
    t4_call_count = 0;
    fake_ms       = 0;

    static BytePipe ab;
    static BytePipe sink;
    ab = {}; sink = {};

    minros::RawNode<> node_a, node_b;
    node_a.transport = {
        .send_bytes = { pipe_send,   &ab },
        .read_bytes = { noop_recv,   nullptr },
        .get_size   = { always_zero, nullptr },
        .get_time   = { get_fake_time, nullptr },
    };
    node_b.transport = {
        .send_bytes = { pipe_send,  &sink },
        .read_bytes = { pipe_recv,  &ab   },
        .get_size   = { pipe_avail, &ab   },
        .get_time   = { get_fake_time, nullptr },
    };

    minros::overlays::reliability::Reliable rel_a{node_a};
    minros::overlays::reliability::Reliable rel_b{node_b};
    rel_b.subscribe(0x04, { t4_on_bytes, nullptr });

    const uint8_t data[] = {0x42};

    // İlk gönderim — callback çağrılmalı
    TEST_ASSERT_TRUE(rel_a.publish(0x04, data, sizeof(data)));
    node_b.spin_once();
    TEST_ASSERT_EQUAL_MESSAGE(1, t4_call_count, "ilk mesajda callback çağrılmadı");

    // ACK node_a'ya ulaşmadığından timeout → otonom retransmit (aynı seq)
    fake_ms = 60;
    node_a.spin_once();
    rel_a.tick();

    // Retransmit edilen frame node_b'ye ulaşır → duplicate → callback çağrılmaz
    node_b.spin_once();
    TEST_ASSERT_EQUAL_MESSAGE(1, t4_call_count, "duplicate mesajda callback çağrılmamalıydı");
}

// ─── Unity giriş noktası ──────────────────────────────────────────────────────
int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_unreliable_raw_bytes);
    RUN_TEST(test_reliable_ack_roundtrip);
    RUN_TEST(test_reliable_timeout_retransmit);
    RUN_TEST(test_reliable_duplicate_filtering);
    return UNITY_END();
}
