#include <Arduino.h>
#include <minros/node.hpp>
#include <minros/std_msgs/twist.hpp>
#include <minros/std_msgs/vector3.hpp>

// ─── Kanal ID'leri ────────────────────────────────────────────────────────────
static constexpr uint8_t CH_IMU_ACCEL = 0x01;  // Vector3 yayını (best-effort)
static constexpr uint8_t CH_CMD_VEL   = 0x02;  // Twist aboneliği (reliable)
static constexpr uint8_t CH_CMD_VEL_P = 0x03;  // Twist yayını   (reliable)

// ─── Node ─────────────────────────────────────────────────────────────────────
static minros::Node<> node;

// ─── Publisher'lar ────────────────────────────────────────────────────────────
static minros::Node<>::Publisher<minros::std_msgs::Vector3> imu_pub;
static minros::Node<>::Publisher<minros::std_msgs::Twist>   cmd_pub;

// ─── Transport callback'leri (UART0) ─────────────────────────────────────────

static void uart_send(uint8_t* buf, uint8_t len, void*) {
    Serial.write(buf, len);
}

static void uart_read(uint8_t* buf, uint8_t len, void*) {
    for (uint8_t i = 0; i < len; i++) {
        buf[i] = static_cast<uint8_t>(Serial.read());
    }
}

static uint8_t uart_available(void*) {
    uint8_t n = Serial.available();
    return n > 255 ? 255 : n;
}

static uint32_t get_millis(void*) {
    return millis();
}

// ─── Subscriber callback'leri ────────────────────────────────────────────────

// CH_CMD_VEL gelince çağrılır (reliable subscriber)
static uint8_t on_cmd_vel(uint8_t seq, uint8_t* payload, uint8_t len, void*) {
    minros::std_msgs::Twist twist;
    if (!twist.from_bytes(payload, len)) return seq;

    Serial.printf("[SUB] cmd_vel — linear=(%.2f, %.2f, %.2f) angular=(%.2f, %.2f, %.2f)\n",
        twist.linear.x,  twist.linear.y,  twist.linear.z,
        twist.angular.x, twist.angular.y, twist.angular.z);

    return seq;
}

// ─── Reliable publisher retransmit callback ───────────────────────────────────

// cmd_pub ACK alınamazsa timeout'ta burayı çağırır; kullanıcı tekrar publish eder
static void on_retransmit(uint8_t ch_id, uint8_t seq, void*) {
    Serial.printf("[RETRANSMIT] ch=%d seq=%d\n", ch_id, seq);

    minros::std_msgs::Twist twist;
    twist.linear.x  = 0.5f;
    twist.linear.y  = 0.0f;
    twist.linear.z  = 0.0f;
    twist.angular.z = 0.1f;
    cmd_pub.publish(twist);
}

// ─── setup ───────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);

    // Transport bağla
    node.transport = {
        .send_bytes = { uart_send,      nullptr },
        .read_bytes = { uart_read,      nullptr },
        .get_size   = { uart_available, nullptr },
        .get_time   = { get_millis,     nullptr },
    };

    // IMU ivme yayıncısı — best-effort
    imu_pub = node.create_publisher<minros::std_msgs::Vector3>(CH_IMU_ACCEL, "imu_accel");

    // Komut yayıncısı — reliable (retransmit callback zorunlu)
    cmd_pub = node.create_publisher<minros::std_msgs::Twist>(
        CH_CMD_VEL_P, "cmd_vel_out",
        /*reliable=*/true,
        { on_retransmit, nullptr }
    );

    // Komut aboneliği — reliable (duplicate filtreli + otomatik ACK)
    node.create_subscription<minros::std_msgs::Twist>(
        CH_CMD_VEL, { on_cmd_vel, nullptr }, /*reliable=*/true
    );

    Serial.println("[minros] node hazır.");
}

// ─── loop ────────────────────────────────────────────────────────────────────

void loop() {
    // Gelen byte'ları işle + reliable timeout tick'leri
    node.spin_once();

    // Her 100 ms'de IMU ivmesini yayınla (best-effort)
    static uint32_t last_imu = 0;
    if (millis() - last_imu >= 100) {
        last_imu = millis();

        minros::std_msgs::Vector3 accel;
        accel.x = 0.01f * (millis() % 100);  // sahte veri
        accel.y = 0.0f;
        accel.z = 9.81f;
        imu_pub.publish(accel);
    }

    // Her 500 ms'de reliable Twist komutu gönder
    static uint32_t last_cmd = 0;
    if (millis() - last_cmd >= 500) {
        last_cmd = millis();

        minros::std_msgs::Twist cmd;
        cmd.linear.x  = 0.5f;
        cmd.angular.z = 0.1f;

        if (!cmd_pub.publish(cmd)) {
            Serial.println("[WARN] cmd_pub: önceki ACK bekleniyor, atlandı.");
        }
    }
}
