#pragma once

#include "minros/utils/utils.hpp"
#include "minros/core/wireframe.hpp"
#include <minros/overlays/logging/logging_protocol.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// logging — minros log overlay'i
//
// reliability gibi bağımsız bir overlay: core'a hiçbir şey eklemez, yalnızca
// RawNode'un public publish/subscribe API'sini kullanır. Log seviyesini + parça
// bilgisini payload'ın önüne 1 baytlık opak FLAGS öneki olarak koyar ve rezerve
// LOG kanalından (CH248) yayar.
//
// İki ayrı sınıf — publisher ve sink'i ayırmak RAM kısıtlı slave'i korur:
//
//   Logger  (publisher, slave): SIFIR buffer tutar. Log'lar fire-and-forget
//           olduğundan payload'ı saklamaz; string literal zaten flash'ta yaşar
//           ve framer oradan byte-byte okur (memory-mapped flash hedeflerinde
//           kopya yok). Kaynakta seviye filtresi ile min_level altı çağrılar
//           wire'a hiç dokunmadan döner.
//
//   LogSink (subscriber, host): parçaları yeniden birleştirir. Yalnızca burada
//           bir reassembly buffer (REASM_BUF) vardır. Slave bunu instantiate
//           etmez → maliyet bindirmez.
//
// ── AVR NOTU ─────────────────────────────────────────────────────────────────
// Bu tasarım flash'ı memory-mapped olan hedefleri varsayar (Cortex-M/STM32,
// ESP32). Klasik AVR (Harvard) mimarisinde flash memory-mapped DEĞİLDİR:
//   • String literal ya başlangıçta RAM'e kopyalanır (RAM israfı),
//   • ya da PROGMEM + pgm_read_byte gerekir; framer'ın düz payload[i] okuması
//     bunu yapamaz.
// Dolayısıyla AVR'de bu logger (özellikle flash'tan zero-copy fragmentation)
// olduğu gibi ÇALIŞMAZ. AVR desteği gerekirse PROGMEM-aware ayrı bir okuma yolu
// tasarlanmalıdır.
//
// Kullanım (slave):
//   RawNode<> node;
//   logging::Logger<decltype(node)> log{node};
//   log.set_min_level(logging::Level::INFO);   // DEBUG bastırılır
//   log.info("motor started");
//   log.error("imu timeout");
//
// Kullanım (host / C++ sink):
//   logging::LogSink<decltype(node)> sink{node};
//   sink.subscribe({on_log, ctx});   // fn(Level, u8* msg, u8 len, void* ctx)
//   node.spin_once();
// ─────────────────────────────────────────────────────────────────────────────

namespace minros {
namespace overlays {
namespace logging {

using Level = protocol::Level;


// ── Publisher — zero-buffer, slave tarafı ────────────────────────────────────
//
// Template parametreler:
//   NodeT      — taşıyıcı node tipi (publish gerektirir)
//   FRAME_DATA — node'un frame DATA bütçesi (CH_ID + head + payload). Node'un
//                MAX_FRAME_DATA'sıyla eşleşmeli; parça boyutu buna göre hesaplanır.
//
template<class NodeT, u8 FRAME_DATA = core::wireframe::MAX_DATA_LEN>
class Logger {
    // Bir parçada taşınabilen text: FRAME_DATA - CH_ID(1) - FLAGS(1).
    static constexpr u8 CHUNK = static_cast<u8>(FRAME_DATA - 2u);
    static_assert(FRAME_DATA >= 3, "FRAME_DATA en az 3 olmalı (CH_ID + FLAGS + >=1 text)");

public:
    explicit Logger(NodeT& node) : node_(&node) {}

    // Kaynakta seviye filtresi: altındaki çağrılar wire'a dokunmadan döner.
    void  set_min_level(Level l) { min_ = l; }
    Level min_level() const      { return min_; }

    // Ham byte log (binary-safe). Uzunsa CHUNK'lara bölünüp SEQ4 ile numaralanır.
    void log(Level l, const u8* msg, u8 len) {
        if (static_cast<u8>(l) < static_cast<u8>(min_)) return;

        u8 off = 0;
        u8 seq = 0;
        for (;;) {
            u8   remain = static_cast<u8>(len - off);
            u8   n      = remain < CHUNK ? remain : CHUNK;
            bool last   = static_cast<u8>(off + n) == len;
            u8   flags  = protocol::pack_flags(l, seq, last);

            node_->publish(protocol::LOG_CHANNEL_ID, &flags, 1, msg + off, n);

            off = static_cast<u8>(off + n);
            seq = static_cast<u8>((seq + 1) & 0x0F);
            if (off >= len) break;   // len==0 dahil: tek boş parça yayılır
        }
    }

    // C-string kolaylıkları — pointer flash'ta olabilir, kopya yok.
    void log(Level l, const char* s) { log(l, reinterpret_cast<const u8*>(s), cstrlen(s)); }
    void debug(const char* s) { log(Level::DEBUG, s); }
    void info (const char* s) { log(Level::INFO,  s); }
    void warn (const char* s) { log(Level::WARN,  s); }
    void error(const char* s) { log(Level::ERROR, s); }
    void fatal(const char* s) { log(Level::FATAL, s); }

private:
    // Freestanding ortamlar için lokal strlen (u8 sınırlı — frame zaten kısa).
    static u8 cstrlen(const char* s) {
        u8 n = 0;
        while (s && s[n] != '\0' && n < 0xFF) ++n;
        return n;
    }

    NodeT* node_;
    Level  min_ = Level::DEBUG;
};


// ── Sink — reassembler, host tarafı ──────────────────────────────────────────
//
// Template parametreler:
//   NodeT     — taşıyıcı node tipi (subscribe gerektirir)
//   REASM_BUF — birleştirme buffer'ı; en uzun beklenen log satırı kadar.
//
template<class NodeT, u8 REASM_BUF = 128>
class LogSink {
public:
    // fn(level, msg, len, ctx) — birleştirilmiş tam satır, FLAGS ayıklanmış.
    using LogCallback = delegate<void, Level, u8*, u8>;

    explicit LogSink(NodeT& node) : node_(&node) {}

    LogSink(const LogSink&)            = delete;   // this pointer'ı node'a kayıtlı
    LogSink& operator=(const LogSink&) = delete;

    bool subscribe(LogCallback cb) {
        cb_ = cb;
        return node_->subscribe(protocol::LOG_CHANNEL_ID, {&LogSink::rx_thunk, this});
    }

    // Kaybedilen (atılan) log sayacı — teşhis için.
    u32 dropped() const { return dropped_; }

private:
    void reset()          { in_msg_ = false; fill_ = 0; }
    void start(Level lvl) { in_msg_ = true; fill_ = 0; expect_seq_ = 0; cur_level_ = lvl; }

    // RawNode ChannelCallback imzası: fn(payload, len, ctx).
    static void rx_thunk(u8* payload, u8 len, void* ctx) {
        auto* s = static_cast<LogSink*>(ctx);
        if (len < 1) return;

        u8    flags = payload[0];
        bool  last  = protocol::flag_last(flags);
        Level lvl   = protocol::flag_level(flags);
        u8    seq   = protocol::flag_seq(flags);
        const u8* data = payload + 1;
        u8        dlen = static_cast<u8>(len - 1);

        // Süreklilik: mesaj seq0 ile başlar; sonraki parçalar expect_seq_ takip eder.
        if (!s->in_msg_ || seq != s->expect_seq_) {
            if (s->in_msg_) s->dropped_++;   // yarım kalmış satır → at
            if (seq != 0) { s->reset(); s->dropped_++; return; }  // ortadan yakalandık
            s->start(lvl);
        }

        // Biriktir (taşarsa satırı at).
        if (static_cast<u16>(s->fill_) + dlen > REASM_BUF) {
            s->reset();
            s->dropped_++;
            return;
        }
        for (u8 i = 0; i < dlen; i++) s->buf_[s->fill_++] = data[i];
        s->expect_seq_ = static_cast<u8>((seq + 1) & 0x0F);

        if (last) {
            if (s->cb_.is_valid()) s->cb_(s->cur_level_, s->buf_, s->fill_);
            s->reset();
        }
    }

    NodeT*      node_;
    LogCallback cb_;
    u8          buf_[REASM_BUF];
    u8          fill_       = 0;
    u8          expect_seq_ = 0;
    Level       cur_level_  = Level::DEBUG;
    bool        in_msg_     = false;
    u32         dropped_    = 0;
};

} // namespace logging
} // namespace overlays
} // namespace minros
