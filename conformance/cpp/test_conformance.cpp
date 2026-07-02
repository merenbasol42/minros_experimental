// minros ↔ minrospy wire-protokolü conformance testi (C++ tarafı).
//
// minros'u, conformance/generate.py'nin ürettiği TARAFSIZ altın vektörlere
// (conformance/vectors.hpp) karşı sınar. Aynı vektörlere Python tarafı da
// (lib/minrospy/tests/test_conformance.py) sınanır; iki implementasyon
// birbirinden kayarsa testlerden biri kırmızıya döner.
//
// PlatformIO/Unity gerektirmez — doğrudan derlenip çalıştırılır:
//   g++ -std=c++17 -I lib/minros conformance/cpp/test_conformance.cpp -o /tmp/ct && /tmp/ct
//
// Çıkış kodu: 0 = tüm vektörler geçti, 1 = en az bir uyuşmazlık.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <minros/core/wireframe.hpp>
#include <minros/core/framer.hpp>

#include "../vectors.hpp"

static int g_failures = 0;

static const char* hex(const uint8_t* p, uint32_t n) {
    static char buf[1024];
    char* w = buf;
    for (uint32_t i = 0; i < n && (w - buf) < 1000; i++)
        w += std::snprintf(w, 4, "%02x", p[i]);
    *w = '\0';
    return buf;
}

static void check_bytes(const char* group, const char* name,
                        const uint8_t* got, uint32_t got_len,
                        const uint8_t* exp, uint32_t exp_len) {
    bool ok = (got_len == exp_len) && (std::memcmp(got, exp, exp_len) == 0);
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL [%s] %s\n", group, name);
        char tmp[1024];
        std::snprintf(tmp, sizeof(tmp), "%s", hex(exp, exp_len));
        std::printf("        beklenen: %s\n", tmp);
        std::printf("        bulunan : %s\n", hex(got, got_len));
    }
}

int main() {
    using namespace minros;

    // ── CRC-8/SMBUS ──────────────────────────────────────────────────────────
    for (uint32_t i = 0; i < conformance::crc8_count; i++) {
        const auto& v = conformance::crc8_vectors[i];
        uint8_t crc = 0;
        for (uint32_t j = 0; j < v.len; j++)
            crc = core::wireframe::crc8_update(crc, v.data[j]);
        check_bytes("crc8", v.name, &crc, 1, &v.crc, 1);
    }

    // ── std_msgs serileştirme ────────────────────────────────────────────────
    conformance::run_message_checks(
        [](const char* name, const auto& msg, const uint8_t* exp, uint32_t exp_len) {
            uint8_t buf[256] = {};
            msg.to_bytes(buf);
            check_bytes("message", name, buf, msg.size(), exp, exp_len);
        });

    // ── Tam wire frame (Framer, opak head = seq baytı) ───────────────────────
    for (uint32_t i = 0; i < conformance::frame_count; i++) {
        const auto& v = conformance::frame_vectors[i];
        core::Framer<> framer;
        uint8_t seq = v.seq;
        // Core SEQ bilmez: seq, payload önüne opak 1-baytlık HEAD olarak verilir.
        bool built = framer.build(v.ch_id, &seq, 1, v.payload, (uint8_t)v.payload_len);
        if (!built) {
            ++g_failures;
            std::printf("  FAIL [frame] %s — Framer::build false döndü\n", v.name);
            continue;
        }
        check_bytes("frame", v.name, framer.data(), framer.size(), v.frame, v.frame_len);
    }

    if (g_failures == 0) {
        std::printf("conformance (C++): tüm vektörler geçti\n");
        return 0;
    }
    std::printf("conformance (C++): %d uyuşmazlık\n", g_failures);
    return 1;
}
