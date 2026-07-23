// ─── minros haberleşme testi ──────────────────────────────────────────────────
//
// Strateji: İki Node<> arasına BytePipe loopback transport bağlanır.
//   node_a  --[a_to_b]--> node_b   (mesaj)
//   node_b  --[b_to_a]--> node_a   (ACK)
//
// Test 1 — Float32 best-effort: pub.publish() → node_b.spin_once() → typed callback
// Test 2 — Twist reliable:      pub.publish() → node_b.spin_once() (callback+ACK)
//                                             → node_a.spin_once() (ACK alındı)
//
// Reliability artık Node'in içindeki reliability::Reliable overlay'i tarafından
// yönetilir; retransmit otonomdur (callback yok), buffer'ı Publisher kendi tutar.
// ─────────────────────────────────────────────────────────────────────────────

#include <unity.h>
#include <cstdint>

#include <minros/node.hpp>
#include <minros/interfaces/std_msgs/primitives.hpp>
#include <minros/interfaces/geometry_msgs/twist.hpp>

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

// ─── Transport callback'leri ──────────────────────────────────────────────────
static void    pipe_send (uint8_t* b, uint8_t n, void* ctx) { static_cast<BytePipe*>(ctx)->write(b, n); }
static void    pipe_recv (uint8_t* b, uint8_t n, void* ctx) { static_cast<BytePipe*>(ctx)->read(b, n);  }
static uint8_t pipe_avail(void* ctx)                        { return static_cast<BytePipe*>(ctx)->available(); }
static uint32_t fake_time(void*)                            { return 0; }

// ─── Yardımcı: iki node'u çift yönlü bağla ───────────────────────────────────
static void connect(minros::Node<>& a, minros::Node<>& b,
                    BytePipe& a_to_b, BytePipe& b_to_a)
{
    a.transport = {
        .send_bytes = { pipe_send,  &a_to_b },
        .read_bytes = { pipe_recv,  &b_to_a },
        .get_size   = { pipe_avail, &b_to_a },
        .get_time   = { fake_time,  nullptr  },
    };
    b.transport = {
        .send_bytes = { pipe_send,  &b_to_a },
        .read_bytes = { pipe_recv,  &a_to_b },
        .get_size   = { pipe_avail, &a_to_b },
        .get_time   = { fake_time,  nullptr  },
    };
}

void setUp()    {}
void tearDown() {}

// ─────────────────────────────────────────────────────────────────────────────
// Test 1 — Float32 best-effort
// ─────────────────────────────────────────────────────────────────────────────

static bool  t1_received = false;
static float t1_value    = 0.0f;

static void t1_on_float32(const minros::interfaces::std_msgs::Float32& msg, void*)
{
    t1_received = true;
    t1_value    = msg.value;
}

void test_float32_best_effort()
{
    t1_received = false;
    t1_value    = 0.0f;

    static BytePipe ab, ba;
    ab = {}; ba = {};

    minros::Node<> node_a, node_b;
    connect(node_a, node_b, ab, ba);

    auto pub = node_a.create_publisher<minros::interfaces::std_msgs::Float32>(0x01);
    node_b.create_subscription<minros::interfaces::std_msgs::Float32>(0x01, { t1_on_float32, nullptr });

    minros::interfaces::std_msgs::Float32 msg;
    msg.value = 3.14f;

    TEST_ASSERT_TRUE_MESSAGE(pub.publish(msg), "publish() false döndü");

    node_b.spin_once();

    TEST_ASSERT_TRUE_MESSAGE(t1_received,  "subscriber callback çağrılmadı");
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.14f, t1_value);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2 — Twist reliable, ACK geri dönüşü
// ─────────────────────────────────────────────────────────────────────────────

static bool  t2_received = false;
static float t2_linear_x = 0.0f;

static void t2_on_twist(const minros::interfaces::geometry_msgs::Twist& msg, void*)
{
    t2_received = true;
    t2_linear_x = msg.linear.x;
}

void test_twist_reliable_with_ack()
{
    t2_received = false;
    t2_linear_x = 0.0f;

    static BytePipe ab, ba;
    ab = {}; ba = {};

    minros::Node<> node_a, node_b;
    connect(node_a, node_b, ab, ba);

    auto pub = node_a.create_publisher<minros::interfaces::geometry_msgs::Twist>(0x02, /*reliable=*/true);
    node_b.create_subscription<minros::interfaces::geometry_msgs::Twist>(
        0x02, { t2_on_twist, nullptr }, /*reliable=*/true
    );

    minros::interfaces::geometry_msgs::Twist cmd;
    cmd.linear.x  = 0.5f;
    cmd.angular.z = 0.1f;

    TEST_ASSERT_TRUE_MESSAGE(pub.publish(cmd), "reliable publish() false döndü");

    node_b.spin_once();  // Twist'i işle → typed callback + ACK gönder
    node_a.spin_once();  // ACK'i al     → ack_pending temizlendi

    TEST_ASSERT_TRUE_MESSAGE(t2_received,  "subscriber callback çağrılmadı");
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, t2_linear_x);

    // ACK alındıktan sonra aynı kanaldan tekrar gönderilebilmeli
    TEST_ASSERT_TRUE_MESSAGE(pub.publish(cmd), "ACK sonrası reliable publish() false döndü");
}

// ─── Unity giriş noktası ──────────────────────────────────────────────────────
int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_float32_best_effort);
    RUN_TEST(test_twist_reliable_with_ack);
    return UNITY_END();
}
