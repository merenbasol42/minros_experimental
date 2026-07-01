#pragma once

#include "minros/utils/utils.hpp"
#include <minros/reliability/reliability_protocol.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// Reliable — minros güvenilirlik (reliability) overlay'i
//
// RawNode'un public pub/sub API'sini kullanan bağımsız bir kullanıcıdır. Core'a
// hiçbir şey eklemez: seq'i, payload'ın önüne opak bir baytlık önek olarak koyar
// ve ACK'i normal bir kanaldan (CH249) yollar. Node gerektirmez — ham RawNode ile
// de kullanılabilir.
//
// Stop-and-wait (window = 1): kanal başına aynı anda en fazla 1 uçuştaki frame.
// Bu yüzden payload KOPYALANMAZ; yalnızca (const u8* + len + seq) tutulur ve
// timeout'ta tick() o pointer'dan kendisi yeniden gönderir.
//
// Kullanıcı sözleşmesi (publisher):
//   publish(ch, buf, len) çağrıldıktan sonra, ACK gelene (can_send(ch) tekrar
//   true olana) kadar buf bozulmamalıdır — Reliable yalnızca pointer'ını tutar.
//
// Kullanım:
//   RawNode<> node;
//   reliability::Reliable rel{node};         // node'a takılır, ACK kanalına abone olur
//
//   rel.subscribe(ch, {on_data, ctx});       // fn(payload, len, ctx) — seq/dedup/ACK gizli
//
//   // loop:
//   node.spin_once();
//   rel.tick();
//
//   if (rel.can_send(ch)) { fill(buf); rel.publish(ch, buf, len); }
//
// Template parametreler:
//   NodeT      — taşıyıcı node tipi (publish/subscribe/transport.get_time gerektirir)
//   MAX_PUB    — güvenilir publisher kanal sayısı
//   MAX_SUB    — güvenilir subscriber kanal sayısı
//   MAX_RETRY  — maksimum retransmit denemesi
//   TIMEOUT_MS — ACK bekleme süresi (ms)
//
// ─────────────────────────────────────────────────────────────────────────────

namespace minros {
namespace reliability {

template<
    class NodeT,
    u8 MAX_PUB    = 4,
    u8 MAX_SUB    = 4,
    u8 MAX_RETRY  = 3,
    u8 TIMEOUT_MS = 50
>
class Reliable {
public:
    // fn(payload, len, ctx) — seq önekı ayıklanmış, kullanıcı verisi.
    using DataCallback = delegate<void, u8*, u8>;

    enum class Error : u8 {
        MAX_RETRIED
    };

    // fn(ch_id, err_code, ctx)
    using ErrorCallback = delegate<void, u8, Error>;


    // node'u tutar ve ACK kanalına (CH249) abone olur.
    explicit Reliable(NodeT& node) : node_(&node) {
        node_->subscribe(protocol::ACK_CHANNEL_ID, {&Reliable::ack_thunk, this});
    }

    Reliable(const Reliable&)            = delete;  // subs_/this pointer'ları node'a kayıtlı
    Reliable& operator=(const Reliable&) = delete;

    void set_err_cb(ErrorCallback cb) { on_err_ = cb; }


    // ── Güvenilir subscriber ───────────────────────────────────────────────
    // Dedup + otomatik ACK içeride; cb yalnızca yeni mesajda çağrılır.
    bool subscribe(u8 ch, DataCallback cb) {
        if (sub_count_ >= MAX_SUB || !cb.is_valid()) return false;

        SubEntry& s = subs_[sub_count_++];
        s.cb       = cb;
        s.owner    = this;
        s.ch       = ch;
        s.last_seq = 0xFF;   // geçersiz başlangıç: ilk mesaj her zaman yeni

        return node_->subscribe(ch, {&Reliable::rx_thunk, &s});
    }


    // ── Güvenilir publisher ────────────────────────────────────────────────

    // Pub slotunu önceden ayır (opsiyonel — publish ilk çağrıda da ayırır).
    bool register_pub(u8 ch) { return get_or_add_pub(ch) != nullptr; }

    // Bu kanalda yeni reliable mesaj gönderilebilir mi (önceki ACK'lendi mi)?
    bool can_send(u8 ch) const {
        const PubEntry* p = find_pub(ch);
        return !p || !p->ack_pending;
    }

    // Güvenilir gönder. ack_pending ise false döner (buf'a dokunma).
    // Reliable yalnızca payload'ın pointer'ını tutar; ACK'e kadar sabit kalmalı.
    bool publish(u8 ch, const u8* payload, u8 len) {
        PubEntry* p = get_or_add_pub(ch);
        if (!p || p->ack_pending) return false;

        ++p->seq;
        p->payload     = payload;
        p->len         = len;
        p->ack_pending = true;
        p->retries     = 0;
        p->sent_at_ms  = now();
        return send(*p);
    }


    // ── Periyodik timeout kontrolü ─────────────────────────────────────────
    // Her ana döngüde çağır.
    void tick() {
        const u32 t = now();
        for (u8 i = 0; i < pub_count_; i++) {
            PubEntry& p = pubs_[i];
            if (!p.ack_pending)                 continue;
            if (t - p.sent_at_ms < TIMEOUT_MS)  continue;

            if (p.retries >= MAX_RETRY) {
                p.ack_pending = false;
                p.retries     = 0;
                if (on_err_.is_valid()) on_err_(p.ch, Error::MAX_RETRIED);
                continue;
            }

            ++p.retries;
            p.sent_at_ms = t;
            send(p);   // aynı pointer'dan otomatik yeniden gönderim — kopya yok
        }
    }


private:
    struct PubEntry {
        const u8* payload     = nullptr;   // kullanıcı buffer'ı — KOPYA DEĞİL
        u32       sent_at_ms  = 0;
        u8        ch          = 0;
        u8        len         = 0;
        u8        seq         = 0;
        u8        retries     = 0;
        bool      ack_pending = false;
    };

    struct SubEntry {
        DataCallback cb;
        Reliable*    owner    = nullptr;
        u8           ch       = 0;
        u8           last_seq = 0xFF;
    };

    NodeT*        node_;
    PubEntry      pubs_[MAX_PUB]{};
    SubEntry      subs_[MAX_SUB]{};
    u8            pub_count_ = 0;
    u8            sub_count_ = 0;
    ErrorCallback on_err_;


    u32 now() const { return node_->transport.get_time(); }

    // seq'i opak bir baytlık head öneki olarak gönder: tele [CH_ID][SEQ][payload].
    bool send(PubEntry& p) {
        u8 seq = p.seq;   // lokal; node->publish framer'a hemen kopyalar
        return node_->publish(p.ch, &seq, 1, p.payload, p.len);
    }

    void send_ack(u8 ch, u8 seq) {
        u8 buf[3] = {
            static_cast<u8>(protocol::ResponseType::ACK),
            ch,
            seq
        };
        node_->publish(protocol::ACK_CHANNEL_ID, buf, 3);  // ACK unreliable, stack yeterli
    }

    PubEntry* find_pub(u8 ch) {
        for (u8 i = 0; i < pub_count_; i++)
            if (pubs_[i].ch == ch) return &pubs_[i];
        return nullptr;
    }
    const PubEntry* find_pub(u8 ch) const {
        for (u8 i = 0; i < pub_count_; i++)
            if (pubs_[i].ch == ch) return &pubs_[i];
        return nullptr;
    }

    PubEntry* get_or_add_pub(u8 ch) {
        if (PubEntry* p = find_pub(ch)) return p;
        if (pub_count_ >= MAX_PUB)      return nullptr;
        PubEntry& p = pubs_[pub_count_++];
        p.ch = ch;
        return &p;
    }


    // ── Statik köprüler (RawNode ChannelCallback imzası: fn(payload, len, ctx)) ──

    // Güvenilir subscriber: seq ayıkla → ACK at → dedup → kullanıcı cb.
    static void rx_thunk(u8* payload, u8 len, void* ctx) {
        auto* s = static_cast<SubEntry*>(ctx);
        if (!s || !s->owner || len < 1) return;

        u8 seq = payload[0];
        s->owner->send_ack(s->ch, seq);   // duplicate olsa da ACK at

        if (seq != s->last_seq) {
            s->last_seq = seq;
            if (s->cb.is_valid()) s->cb(payload + 1, static_cast<u8>(len - 1));
        }
    }

    // ACK alımı (CH249). Payload: [RESP][acked_ch][acked_seq].
    static void ack_thunk(u8* payload, u8 len, void* ctx) {
        if (len < 3) return;
        auto* self = static_cast<Reliable*>(ctx);

        if (payload[0] != static_cast<u8>(protocol::ResponseType::ACK)) return;

        u8 acked_ch  = payload[1];
        u8 acked_seq = payload[2];

        PubEntry* p = self->find_pub(acked_ch);
        if (p && p->ack_pending && p->seq == acked_seq) {
            p->ack_pending = false;
            p->retries     = 0;
        }
    }
};

} // namespace reliability
} // namespace minros
