// ─── minros Parser dayanıklılık testleri ──────────────────────────────────────
//
// host/test/test_parser_resilience.py ile birebir aynı senaryoları C++
// Parser'a karşı sınar — iki implementasyonun (minros ↔ minrospy) resync
// davranışı birbirinden kaymasın diye. Donanım gerekmez:
//
//   pio test -e native -f test_parser_resilience
//
// CRC_MISMATCH / INVALID_LENGTH sonrası Parser artık sadece eşleşmiş 4 baytlık
// HEADER'ı atıp (kendi içinde çakışmadığı için "header değil" olduğu kesin
// olan tek bölge) kalan LEN+DATA+CRC baytlarını HEADER_WAIT'ten yeniden tarar
// — gömülü gerçek bir frame'in header'ı kaçırılmaz.
// ─────────────────────────────────────────────────────────────────────────────

#include <unity.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <random>
#include <algorithm>

#include <minros/core/parser.hpp>
#include <minros/core/framer.hpp>

using namespace minros;
using Error = core::Parser<>::Error;

// ─── Collector: frame/hata callback'lerini sıraya göre toplar ────────────────
struct Collector {
    std::vector<std::vector<u8>> frames;
    std::vector<Error>           errors;
};

static void on_frame(u8* buf, u8 start, u8 len, void* ctx) {
    static_cast<Collector*>(ctx)->frames.emplace_back(buf + start, buf + start + len);
}

static void on_error(Error e, void* ctx) {
    static_cast<Collector*>(ctx)->errors.push_back(e);
}

static core::Parser<> make_parser(Collector& c) {
    core::Parser<> p;
    p.set_on_frame_completed({ on_frame, &c });
    p.set_on_error({ on_error, &c });
    return p;
}

// ─── Yardımcılar ───────────────────────────────────────────────────────────

static void feed(core::Parser<>& p, const std::vector<u8>& bytes) {
    size_t off = 0;
    while (off < bytes.size()) {
        auto w = p.write_window();
        TEST_ASSERT_MESSAGE(w.size > 0, "write_window bos donuyor - parser sikisti");
        u8 n = static_cast<u8>(std::min<size_t>(w.size, bytes.size() - off));
        std::memcpy(w.data, bytes.data() + off, n);
        p.commit(n);
        off += n;
    }
}

static std::vector<u8> ch_payload(u8 ch_id, const char* payload) {
    std::vector<u8> v{ ch_id };
    for (const char* p = payload; *p; p++) v.push_back(static_cast<u8>(*p));
    return v;
}

static std::vector<u8> build_frame(u8 ch_id, const char* payload) {
    core::Framer<> f;
    bool ok = f.build(ch_id, reinterpret_cast<const u8*>(payload),
                       static_cast<u8>(std::strlen(payload)));
    TEST_ASSERT_TRUE_MESSAGE(ok, "Framer::build basarisiz oldu");
    return std::vector<u8>(f.data(), f.data() + f.size());
}

static u8 crc8_over(const std::vector<u8>& data) {
    u8 crc = 0;
    for (u8 b : data) crc = core::wireframe::crc8_update(crc, b);
    return crc;
}

static std::vector<u8> concat(std::initializer_list<std::vector<u8>> parts) {
    std::vector<u8> out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

static void assert_frame(const std::vector<u8>& got, const std::vector<u8>& expected) {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)expected.size(), (uint32_t)got.size(), "frame uzunlugu uyusmuyor");
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected.data(), got.data(), expected.size(), "frame icerigi uyusmuyor");
}

static void assert_frames(const std::vector<std::vector<u8>>& got,
                           const std::vector<std::vector<u8>>& expected) {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)expected.size(), (uint32_t)got.size(), "frame sayisi uyusmuyor");
    for (size_t i = 0; i < expected.size(); i++) {
        assert_frame(got[i], expected[i]);
    }
}

static void assert_errors(const std::vector<Error>& got, const std::vector<Error>& expected) {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)expected.size(), (uint32_t)got.size(), "hata sayisi uyusmuyor");
    for (size_t i = 0; i < expected.size(); i++) {
        TEST_ASSERT_EQUAL_MESSAGE((int)expected[i], (int)got[i], "hata tipi uyusmuyor");
    }
}

void setUp()    {}
void tearDown() {}

// ── Hızlı toparlanma: hata anında state resetlenir ──────────────────────────

void test_recovers_after_random_noise_prefix() {
    Collector c;
    auto p = make_parser(c);

    std::mt19937 rng(42);
    std::vector<u8> noise;
    while (noise.size() < 300) {
        u8 b = static_cast<u8>(rng() % 256);
        bool is_header_byte = false;
        for (u8 i = 0; i < core::wireframe::HEADER_SIZE; i++) {
            if (b == core::wireframe::HEADER[i]) { is_header_byte = true; break; }
        }
        if (!is_header_byte) noise.push_back(b);
    }

    auto frame = build_frame(7, "abc");
    feed(p, concat({ noise, frame }));

    assert_frames(c.frames, { ch_payload(7, "abc") });
    assert_errors(c.errors, {});
}

void test_recovers_immediately_after_invalid_length() {
    Collector c;
    auto p = make_parser(c);

    std::vector<u8> bad(core::wireframe::HEADER, core::wireframe::HEADER + core::wireframe::HEADER_SIZE);
    bad.push_back(0);  // 0 < MIN_DATA_LEN
    auto good = build_frame(3, "xy");

    feed(p, concat({ bad, good }));

    assert_errors(c.errors, { Error::INVALID_LENGTH });
    assert_frames(c.frames, { ch_payload(3, "xy") });
}

void test_recovers_immediately_after_crc_mismatch() {
    Collector c;
    auto p = make_parser(c);

    auto corrupt = build_frame(5, "data");
    corrupt.back() ^= 0xFF;  // sadece CRC baytını boz
    auto good = build_frame(9, "ok");

    feed(p, concat({ corrupt, good }));

    assert_errors(c.errors, { Error::CRC_MISMATCH });
    assert_frames(c.frames, { ch_payload(9, "ok") });
}

void test_crc_failure_rescans_consumed_bytes_and_recovers_embedded_frame() {
    Collector c;
    auto p = make_parser(c);

    auto inner       = build_frame(9, "AB");  // tek başına ele alınsa geçerli olacak bir frame
    auto& outer_data = inner;                 // ama burada dış frame'in ham DATA'sı olarak gömülü
    u8 wrong_crc = static_cast<u8>(crc8_over(outer_data) + 1);

    std::vector<u8> outer(core::wireframe::HEADER, core::wireframe::HEADER + core::wireframe::HEADER_SIZE);
    outer.push_back(static_cast<u8>(outer_data.size()));
    outer.insert(outer.end(), outer_data.begin(), outer_data.end());
    outer.push_back(wrong_crc);

    auto recovery = build_frame(77, "sonraki");

    feed(p, concat({ outer, recovery }));

    // inner, dış (bozuk) frame'in DATA'sı içinde gömülüydü ama resync onu da buldu.
    assert_frames(c.frames, { ch_payload(9, "AB"), ch_payload(77, "sonraki") });
    assert_errors(c.errors, { Error::CRC_MISMATCH });
}

void test_overlapping_header_prefix_resyncs() {
    Collector c;
    auto p = make_parser(c);

    auto frame = build_frame(4, "hey");
    std::vector<u8> stream{ 'm', 'r' };  // "mr" + "mros..." -> içeride "m,r,m,r,o,s" çakışması
    stream.insert(stream.end(), frame.begin(), frame.end());

    feed(p, stream);

    assert_frames(c.frames, { ch_payload(4, "hey") });
    assert_errors(c.errors, {});
}

// ── Asıl soru: yarım kalmış (tamamlanamayan) bir frame'in maliyeti ─────────

void test_truncated_frame_recovers_via_embedded_rescan() {
    Collector c;
    auto p = make_parser(c);

    std::vector<u8> truncated(core::wireframe::HEADER, core::wireframe::HEADER + core::wireframe::HEADER_SIZE);
    truncated.push_back(10);                     // 10 ilan edildi
    truncated.insert(truncated.end(), 4, 0xAA);   // 4 geldi, kesildi

    auto frame_a = build_frame(1, "AAAA");  // DATA'ya gömülü kalır ama resync ile kurtarılır
    auto frame_b = build_frame(2, "BBBB");  // akış zaten bununla toparlanacaktı

    feed(p, concat({ truncated, frame_a, frame_b }));

    assert_frames(c.frames, { ch_payload(1, "AAAA"), ch_payload(2, "BBBB") });
    assert_errors(c.errors, { Error::CRC_MISMATCH });
}

void test_truncated_frame_never_permanently_locks() {
    Collector c;
    auto p = make_parser(c);

    std::vector<u8> stream;
    for (int i = 0; i < 20; i++) {
        u8 declared_len = static_cast<u8>(5 + (i % 5));
        u8 sent         = static_cast<u8>(declared_len - 1);  // her zaman en az 1 bayt eksik bırak
        stream.insert(stream.end(), core::wireframe::HEADER, core::wireframe::HEADER + core::wireframe::HEADER_SIZE);
        stream.push_back(declared_len);
        stream.insert(stream.end(), sent, 0xBB);
    }

    auto good = build_frame(42, "toparlandi");
    stream.insert(stream.end(), good.begin(), good.end());

    feed(p, stream);

    assert_frames(c.frames, { ch_payload(42, "toparlandi") });
}

void test_large_declared_length_can_silently_swallow_several_frames() {
    // DİKKAT — kütüphanenin resync'in kapsamadığı kırılgan noktası: declared_len
    // tamamlanana kadar CRC kontrolüne hiç ulaşılmıyor, dolayısıyla resync de
    // devreye girmiyor. Bu, Parser'ın timeout bilmemesinden kaynaklanan ayrı bir
    // sorun — bu testin amacı bunu hâlâ belgelemek.
    Collector c;
    auto p = make_parser(c);

    u8 declared_len = core::wireframe::MAX_DATA_LEN;
    std::vector<u8> truncated(core::wireframe::HEADER, core::wireframe::HEADER + core::wireframe::HEADER_SIZE);
    truncated.push_back(declared_len);  // veri hiç gelmeden kesildi

    std::vector<u8> swallowed_bytes;
    for (u8 i = 0; i < 5; i++) {
        auto f = build_frame(i, "xxxx");  // bunların hepsi kaybolacak
        swallowed_bytes.insert(swallowed_bytes.end(), f.begin(), f.end());
    }

    feed(p, concat({ truncated, swallowed_bytes }));

    // declared_len (249) henüz tamamlanmadı -> CRC kontrolüne hiç ulaşılmadı,
    // dolayısıyla ne bir frame ne de bir hata sinyali var. Sessiz veri kaybı.
    assert_frames(c.frames, {});
    assert_errors(c.errors, {});
}

void test_recovery_boundary_no_longer_loses_any_frame() {
    // declared_len tam tamamlandığı an CRC_WAIT'e geçilir ve HEMEN sıradaki bayt
    // CRC adayı olarak tüketilir — bu bayt genelde bir sonraki geçerli frame'in
    // HEADER'ının ilk baytıdır, bu yüzden CRC_MISMATCH kaçınılmaz. Ama artık bu
    // hata bir kayıp anlamına gelmiyor: declared_len boyunca tüketilen 249
    // baytlık DATA içine gömülü TÜM frame'ler resync tarafından tek tek bulunup
    // teslim ediliyor.
    Collector c;
    auto p = make_parser(c);

    u8 declared_len = core::wireframe::MAX_DATA_LEN;
    std::vector<u8> truncated(core::wireframe::HEADER, core::wireframe::HEADER + core::wireframe::HEADER_SIZE);
    truncated.push_back(declared_len);

    std::vector<u8> swallowed_bytes;
    for (u8 i = 0; i < 5; i++) {
        auto f = build_frame(i, "xxxx");
        swallowed_bytes.insert(swallowed_bytes.end(), f.begin(), f.end());
    }

    auto also_lost = build_frame(99, "finally-ok");  // artık kaybolmuyor: declared_len sınırına denk gelse de resync yakalıyor
    u8 pad_len = static_cast<u8>(declared_len - swallowed_bytes.size() - also_lost.size());
    std::vector<u8> pad(pad_len, 0xEE);  // declared_len'i tam tamamlamak için dolgu

    auto frame_after_boundary = build_frame(55, "still-lost");  // artık kaybolmuyor: resync ile kurtarılıyor
    auto frame_recovered      = build_frame(66, "recovered!");

    feed(p, concat({ truncated, swallowed_bytes, also_lost, pad, frame_after_boundary, frame_recovered }));

    assert_frames(c.frames, {
        ch_payload(0, "xxxx"), ch_payload(1, "xxxx"), ch_payload(2, "xxxx"),
        ch_payload(3, "xxxx"), ch_payload(4, "xxxx"),
        ch_payload(99, "finally-ok"),
        ch_payload(55, "still-lost"),
        ch_payload(66, "recovered!"),
    });
    assert_errors(c.errors, { Error::CRC_MISMATCH });
}

// ── Buffer sıkışması: sürekli gürültü write_window()'u kilitlememeli ───────
//
// Parser'ın buffer'ı sabit boyutlu (BUFFER_SIZE) ve lineer. HEADER_WAIT'te
// hiçbir şeye uymayan baytlar consume()/resync() tetiklemez; compact()
// olmasaydı bu baytlar buffer'da fiziksel olarak birikir, write_window()
// bir noktada kalıcı olarak 0'a düşer ve gerçek kullanımda (raw_node.hpp'nin
// feed_parser()'ı) parser artık hiç bayt okumaz — kalıcı kilitlenme.

void test_sustained_noise_never_stalls_write_window() {
    Collector c;
    auto p = make_parser(c);

    // BUFFER_SIZE'ın (255, varsayılan MAX_DATA ile) ~20 katı, header'a hiç
    // uymayan kesintisiz bir gürültü akışı. feed() içindeki assert, write_window
    // hiç kalıcı olarak 0'a saplanmazsa geçer.
    std::mt19937 rng(7);
    std::vector<u8> noise;
    while (noise.size() < 5000) {
        u8 b = static_cast<u8>(rng() % 256);
        bool is_header_byte = false;
        for (u8 i = 0; i < core::wireframe::HEADER_SIZE; i++) {
            if (b == core::wireframe::HEADER[i]) { is_header_byte = true; break; }
        }
        if (!is_header_byte) noise.push_back(b);
    }

    feed(p, noise);

    // Gürültüden hemen sonra gelen geçerli frame de sorunsuz yakalanmalı.
    auto frame = build_frame(11, "hala-calisiyor");
    feed(p, frame);

    assert_frames(c.frames, { ch_payload(11, "hala-calisiyor") });
    assert_errors(c.errors, {});
}

void test_compact_keeps_write_window_near_full_capacity() {
    // Sadece feed()'in çökmediğini değil, compact()'in gerçekten çalıştığını
    // doğrudan write_window() boyutuna bakarak kanıtlar: uzun bir gürültü
    // parçasından sonra write_window() başlangıç kapasitesine yakın kalmalı —
    // compact() en fazla (HEADER_SIZE - 1) baytlık bir kuyruk bırakabilir.
    Collector c;
    auto p = make_parser(c);

    u8 initial_capacity = p.write_window().size;

    std::mt19937 rng(99);
    std::vector<u8> noise;
    while (noise.size() < 2000) {
        u8 b = static_cast<u8>(rng() % 256);
        bool is_header_byte = false;
        for (u8 i = 0; i < core::wireframe::HEADER_SIZE; i++) {
            if (b == core::wireframe::HEADER[i]) { is_header_byte = true; break; }
        }
        if (!is_header_byte) noise.push_back(b);
    }

    feed(p, noise);

    u8 remaining_capacity = p.write_window().size;
    TEST_ASSERT_GREATER_OR_EQUAL_UINT8(initial_capacity - (core::wireframe::HEADER_SIZE - 1), remaining_capacity);
}

void test_garbage_prefix_does_not_starve_a_large_frame_mid_reception() {
    // Kritik senaryo: buffer'ın çoğu (200 bayt) gürültüyle dolu, ardından TAM
    // BOYUTLU (MAX_DATA_LEN) bir frame geliyor. Garbage + header + tüm DATA + CRC
    // toplamda BUFFER_SIZE'ı (255) ciddi şekilde aşıyor (200+5+249+1=455). compact()
    // sadece HEADER_WAIT'te değil, LENGTH_WAIT/DATA_READING/CRC_WAIT sırasında da
    // çalışmasaydı, header bulunduktan sonraki write_window() çağrıları hâlâ 200
    // baytlık gürültüyü tutuyor olacağından DATA'nın tamamı için asla yeterli yer
    // açılamaz — feed() (dolayısıyla gerçek transport okuma döngüsü) tıkanırdı.
    Collector c;
    auto p = make_parser(c);

    std::mt19937 rng(123);
    std::vector<u8> noise;
    while (noise.size() < 200) {
        u8 b = static_cast<u8>(rng() % 256);
        bool is_header_byte = false;
        for (u8 i = 0; i < core::wireframe::HEADER_SIZE; i++) {
            if (b == core::wireframe::HEADER[i]) { is_header_byte = true; break; }
        }
        if (!is_header_byte) noise.push_back(b);
    }

    std::string big_payload(core::wireframe::MAX_DATA_LEN - 1, 'z');  // CH_ID(1) + payload = MAX_DATA_LEN
    auto frame = build_frame(88, big_payload.c_str());

    feed(p, concat({ noise, frame }));  // feed() içindeki assert, tıkanma olursa testi düşürür

    assert_frames(c.frames, { ch_payload(88, big_payload.c_str()) });
    assert_errors(c.errors, {});
}

// ── Beslenme granülerliği state machine'i etkilememeli ─────────────────────

void test_byte_by_byte_feed_matches_bulk_feed() {
    std::vector<u8> stream;
    const char* garbage = "garbage-before";
    for (const char* p = garbage; *p; p++) stream.push_back(static_cast<u8>(*p));

    auto f1 = build_frame(1, "one");
    stream.insert(stream.end(), f1.begin(), f1.end());
    auto f2 = build_frame(2, "two-two");
    stream.insert(stream.end(), f2.begin(), f2.end());

    stream.insert(stream.end(), core::wireframe::HEADER, core::wireframe::HEADER + core::wireframe::HEADER_SIZE);
    stream.push_back(255);  // geçersiz LEN (> MAX_DATA_LEN)

    auto f3 = build_frame(3, "three");
    stream.insert(stream.end(), f3.begin(), f3.end());

    Collector bulk_c;
    auto p_bulk = make_parser(bulk_c);
    feed(p_bulk, stream);

    Collector byte_c;
    auto p_byte = make_parser(byte_c);
    for (u8 b : stream) {
        feed(p_byte, { b });
    }

    assert_frames(bulk_c.frames, { ch_payload(1, "one"), ch_payload(2, "two-two"), ch_payload(3, "three") });
    assert_frames(byte_c.frames, bulk_c.frames);
    assert_errors(bulk_c.errors, { Error::INVALID_LENGTH });
    assert_errors(byte_c.errors, bulk_c.errors);
}

// ─── Unity giriş noktası ────────────────────────────────────────────────────
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_recovers_after_random_noise_prefix);
    RUN_TEST(test_recovers_immediately_after_invalid_length);
    RUN_TEST(test_recovers_immediately_after_crc_mismatch);
    RUN_TEST(test_crc_failure_rescans_consumed_bytes_and_recovers_embedded_frame);
    RUN_TEST(test_overlapping_header_prefix_resyncs);
    RUN_TEST(test_truncated_frame_recovers_via_embedded_rescan);
    RUN_TEST(test_truncated_frame_never_permanently_locks);
    RUN_TEST(test_large_declared_length_can_silently_swallow_several_frames);
    RUN_TEST(test_recovery_boundary_no_longer_loses_any_frame);
    RUN_TEST(test_sustained_noise_never_stalls_write_window);
    RUN_TEST(test_compact_keeps_write_window_near_full_capacity);
    RUN_TEST(test_garbage_prefix_does_not_starve_a_large_frame_mid_reception);
    RUN_TEST(test_byte_by_byte_feed_matches_bulk_feed);
    return UNITY_END();
}
