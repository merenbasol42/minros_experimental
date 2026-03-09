#pragma once

#include "minros/utils/utils.hpp"
#include <minros/reliability/reliability_protocol.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// Sequencer — minros reliability katmanı
//
// Sadece state yönetimi yapar: seq numaraları, ACK takibi, timeout.
// Frame göndermez — sadece ACK frame'leri için AckSendCallback kullanır.
// Kullanıcı verisi Publisher üzerinden gönderilir, buradan değil.
//
// Publisher akışı:
//   1. acquire_seq(ch_id)          → {success, seq} — ACK bekleniyorsa false döner
//   2. node.publish(seq, ...)   → Publisher gönderir
//   3. notify_sent(ch_id, now)  → timeout sayacı başlar
//
// Timeout olursa:
//   retransmit_cb(ch_id, seq) çağrılır — kullanıcı Publisher::publish'i tekrar çağırır.
//   acquire_seq retransmit modunda seq'i artırmaz, aynı seq'i verir.
//
// Subscriber tarafı:
//   Gelen seq kontrol edilir; duplicate ise yok sayılır.
//   Her iki durumda da CH249 üzerinden ACK otomatik gönderilir (AckSendCallback).
//
// Template parametreler:
//   MAX_PUB    — güvenilir publisher kanal sayısı
//   MAX_SUB    — güvenilir subscriber kanal sayısı
//   MAX_RETRY  — max retransmit denemesi
//   TIMEOUT_MS — ACK bekleme süresi (ms)
//
// ─────────────────────────────────────────────────────────────────────────────

namespace minros {
namespace reliability {

template<
    u8 MAX_PUB  = 8,
    u8 MAX_SUB  = 8,
    u8 MAX_RETRY  = 3,
    u8 TIMEOUT_MS = 50
>
class Sequencer {
public:
    struct SeqSlot {
        bool success;
        u8   seq;
    };

    enum class ErrorCode : u8 {
        MAX_RETRIED
    };

    // ── delegate türleri ──────────────────────────────────────────────────────

    // Broker ChannelCallback ile aynı imza: fn(seq, payload, len, ctx)
    using ChannelCallback = utils::delegate<u8, u8*, u8>;

    // Sadece ACK frame'leri göndermek için: fn(ch_id, seq, payload, len, ctx)
    // Kullanıcı veri frame'leri Publisher tarafından gönderilir — buradan değil.
    using AckSendCallback = utils::delegate<u8, u8, u8*, u8>;

    // fn(ch_id, seq, ctx) — timeout olunca çağrılır.
    // Kullanıcı bu callback içinde Publisher::publish'i tekrar çağırır.
    // acquire_seq retransmit modunda aynı seq'i verir, seq artırmaz.
    using RetransmitCallback = utils::delegate<void, u8, u8>;

    // fn(ch_id, seq, err_code, ctx)
    using ErrorCallback = utils::delegate<u8, u8, ErrorCode>;


    // ── Yapılandırma ──────────────────────────────────────────────────────────

    // Sadece ACK frame'leri göndermek için köprüyü ver.
    // Node'daki ack_publish_cb buraya bağlanır.
    void set_ack_sender(AckSendCallback cb) { ack_send_cb_ = cb; }

    void set_err_cb(ErrorCallback cb) { on_err_ = cb; }


    // ── Güvenilir subscriber oluştur ──────────────────────────────────────────
    //
    // Dönen ChannelCallback'yi broker.subscribe(ch_id, ...) 'e ilet.
    // Duplicate seq kontrolü + otomatik ACK bu wrapper içinde yapılır.
    //
    ChannelCallback make_reliable_sub(u8 ch_id, ChannelCallback user_cb) {
        if (sub_count_ >= MAX_SUB) return {};

        SubEntry& s = subs_[sub_count_++];
        s.ch_id    = ch_id;
        s.last_seq = 0xFF;   // geçersiz başlangıç: ilk mesaj her zaman yeni
        s.user_cb  = user_cb;
        s.owner    = this;

        return {&Sequencer::on_frame, &s};
    }


    // ── Güvenilir publisher kanalı kaydet ─────────────────────────────────────
    //
    // retransmit_cb: fn(ch_id, seq) — timeout'ta çağrılır.
    //   Kullanıcı callback içinde Publisher::publish'i tekrar çağırır.
    //
    bool register_pub(u8 ch_id, RetransmitCallback retransmit_cb) {
        if (pub_count_ >= MAX_PUB || !retransmit_cb.is_valid()) return false;

        PubEntry& p      = pubs_[pub_count_++];
        p.ch_id          = ch_id;
        p.seq            = 0;
        p.ack_pending        = false;
        p.retry_count    = 0;
        p.sent_at_ms     = 0;
        p.retransmit_pending = false;
        p.retransmit_cb  = retransmit_cb;
        return true;
    }


    // ── Seq numarası al ───────────────────────────────────────────────────────
    //
    // Publisher, frame göndermeden önce bunu çağırır.
    //   • Normal modda: seq artırılır, ack_pending=true yapılır, timeout başlar.
    //   • Retransmit modunda: seq artırılmaz, aynı seq verilir.
    //     (tick() retransmit_cb'den önce bu modu açar.)
    //   • ack_pending=true iken: {false, 0} döner — ACK bekleniyor.
    //
    SeqSlot acquire_seq(u8 ch_id, u32 now_ms) {
        PubEntry* p = find_pub(ch_id);
        if (!p || p->ack_pending) return {false, 0};

        p->ack_pending = true;
        p->sent_at_ms = now_ms;
        if (!p->retransmit_pending) {
            ++p->seq;
        }
        p->retransmit_pending = false;
        return {true, p->seq};
    }


    // ── ACK kanalı için ChannelCallback ───────────────────────────────────────
    //
    // node.subscribe(protocol::ACK_CHANNEL_ID, seq.ack_callback()) ile kullan.
    //
    ChannelCallback ack_callback() {
        return {&Sequencer::ack_wrap, this};
    }


    // ── Periyodik timeout kontrolü ────────────────────────────────────────────
    //
    // Her ana döngüde çağır: seq.tick(millis())
    //
    void tick(u32 now_ms) {
        for (u8 i = 0; i < pub_count_; i++) {
            PubEntry& p = pubs_[i];
            if (!p.ack_pending) continue;
            if (now_ms - p.sent_at_ms < TIMEOUT_MS) continue;

            if (p.retry_count >= MAX_RETRY) {
                p.ack_pending     = false;
                p.retry_count = 0;
                if (on_err_.is_valid()) on_err_(p.ch_id, p.seq, ErrorCode::MAX_RETRIED);
                continue;
            }

            ++p.retry_count;

            // ack_pending'i temizle ve retransmit_pending'u aç:
            // kullanıcı retransmit_cb içinde Publisher::publish çağıracak,
            // acquire_seq aynı seq'i verecek ve seq artırmayacak.
            p.ack_pending          = false;
            p.retransmit_pending  = true;

            if (p.retransmit_cb.is_valid()) {
                p.retransmit_cb(p.ch_id, p.seq);
            }
        }
    }


private:
    // ── İç yapılar ────────────────────────────────────────────────────────────

    struct SubEntry {
        ChannelCallback user_cb;
        Sequencer*      owner    = nullptr;
        u8              ch_id    = 0;
        u8              last_seq = 0xFF;
    };

    struct PubEntry {
        RetransmitCallback retransmit_cb;
        u32                sent_at_ms     = 0;
        u8                 ch_id          = 0;
        u8                 seq            = 0;
        u8                 retry_count    = 0;
        bool               ack_pending        = false;
        bool               retransmit_pending = false;
    };

    SubEntry        subs_[MAX_SUB]{};
    PubEntry        pubs_[MAX_PUB]{};
    u8              sub_count_   = 0;
    u8              pub_count_   = 0;
    AckSendCallback ack_send_cb_;
    ErrorCallback   on_err_;


    // ── Yardımcılar ───────────────────────────────────────────────────────────

    PubEntry* find_pub(u8 ch_id) {
        for (u8 i = 0; i < pub_count_; i++) {
            if (pubs_[i].ch_id == ch_id) return &pubs_[i];
        }
        return nullptr;
    }

    // CH249 üzerinden ACK gönder. Payload: [RESP=0x06][ch_id][seq]
    // Bu fonksiyon sadece subscriber tarafından çağrılır.
    void send_ack(u8 ch_id, u8 seq) {
        if (!ack_send_cb_.is_valid()) return;
        u8 buf[3] = {
            static_cast<u8>(protocol::ResponseType::ACK),
            ch_id,
            seq
        };
        ack_send_cb_(protocol::ACK_CHANNEL_ID, 0, buf, 3);
    }


    // ── Statik köprüler ───────────────────────────────────────────────────────

    // Broker → subscriber wrapper
    static void on_frame(u8 seq, u8* payload, u8 len, void* ctx) {
        auto* s = static_cast<SubEntry*>(ctx);
        if (!s || !s->owner) return;

        bool is_dup = (seq == s->last_seq);

        // Duplicate olsa da ACK gönder — karşı taraf timeout'tan korunur.
        s->owner->send_ack(s->ch_id, seq);

        if (!is_dup) {
            s->last_seq = seq;
            if (s->user_cb.is_valid()) {
                s->user_cb(seq, payload, len);
            }
        }
    }

    // Broker → ACK alımı (CH249)
    // Payload: [RESP=0x06][acked_ch][acked_seq]
    static void ack_wrap(u8 /*seq*/, u8* payload, u8 len, void* ctx) {
        if (len < 3) return;
        auto* self = static_cast<Sequencer*>(ctx);

        u8 acked_type = payload[0];
        u8 acked_ch   = payload[1];
        u8 acked_seq  = payload[2];
        
        if (acked_type != static_cast<u8>(protocol::ResponseType::ACK)) {return;}

        PubEntry* p = self->find_pub(acked_ch);
        if (p && p->seq == acked_seq) {
            p->ack_pending     = false;
            p->retry_count = 0;
        }
    }
};

} // namespace reliability
} // namespace minros
