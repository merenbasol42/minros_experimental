#include <Arduino.h>


#include <minros/node.hpp>
#include <minros/std_msgs/vector3.hpp>


minros::Node<> node;


inline u8 tp_get_size(void* ctx) {
    return static_cast<u8>(Serial.available());
}

inline u32 tp_get_time(void* ctx) {
    return millis();
}

inline void tp_read_bytes(u8* buffer, u8 count, void* ctx) {
    Serial.readBytes(buffer, count);
}

inline void tp_send_bytes(u8* buffer, u8 count, void* ctx) {
    Serial.write(buffer, count);
}


void setup() {
    Serial.begin();

    node.transport = {
        .send_bytes = { tp_send_bytes, nullptr },
        .read_bytes = { tp_read_bytes, nullptr },
        .get_size   = { tp_get_size,   nullptr },
        .get_time   = { tp_get_time,   nullptr },
    };

    auto p = node.create_publisher<minros::std_msgs::Vector3>(1, "");

}

void loop() {
    node.spin_once();
}