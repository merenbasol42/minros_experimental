#pragma once

#include <minros/utils/utils.hpp>
#include <minros/overlays/parameters/parameters_protocol.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// Params — minros parameters overlay (düğüm tarafı)
//
// Düğümün parametrelerini host'a okunur/yazılır kılar. Bir "kanal katmanı"
// (ChannelT) üzerinden çalışır; bu katman subscribe/publish sağlayan herhangi
// bir şeydir — RawNode (best-effort) ya da reliability::Reliable (güvenilir).
// Overlay core'a hiçbir şey eklemez; PARAM_REQ'e abone olur, PARAM_RES'ten yanıt
// yollar.
//
// register_param<MsgT>(id, &var) tip bilgisini (FAMILY_ID/TYPE_ID/SIZE) MsgT'den
// derleme zamanında çıkarır ve serileştirmeyi MsgT::to_bytes/from_bytes'a bağlar
// (tip-silme). Primitive'ler de birer mesajdır (Float32, Int32, …) → tek yol hem
// skaler hem kompoziti kapsar.
//
// Kullanım:
//   RawNode<> node;                      // ya da Reliable
//   parameters::Params params{node};     // PARAM_REQ'e abone olur (CTAD)
//
//   std_msgs::Float32 kp;
//   params.register_param(5, &kp);                 // okunur/yazılır
//   params.register_param(6, &fw_version, true);   // salt-okunur
//
// Notlar:
//   • Giden yanıt buffer'ı üye olarak tutulur (reliability pointer'ı ACK'e kadar
//     saklar → buffer kalıcı olmalı). Stop-and-wait istek/yanıt akışında güvenli.
//   • Değer boyutu MAX_VALUE ile sınırlıdır (register_param'da static_assert).
// ─────────────────────────────────────────────────────────────────────────────

namespace minros {
namespace overlays {
namespace parameters {

template<class ChannelT, u8 MAX_PARAMS = 8, u8 MAX_VALUE = 64>
class Params {
    static_assert(MAX_PARAMS > 0, "MAX_PARAMS en az 1 olmalı");

public:
    explicit Params(ChannelT& ch) : ch_(&ch) {
        ch_->subscribe(protocol::PARAM_REQ_CHANNEL_ID, {&Params::req_thunk, this});
    }

    // subs_/this pointer'ları kanala kayıtlı → kopyalanamaz.
    Params(const Params&)            = delete;
    Params& operator=(const Params&) = delete;

    // Parametreyi kaydet. storage, MsgT tipinde gerçek değişkendir (kopya yok).
    template<typename MsgT>
    bool register_param(u8 id, MsgT* storage, bool read_only = false) {
        static_assert(MsgT::SIZE <= MAX_VALUE, "param değeri MAX_VALUE'yi aşıyor");
        if (count_ >= MAX_PARAMS || storage == nullptr) return false;

        Entry& e     = entries_[count_++];
        e.id         = id;
        e.family_id  = MsgT::family_id();
        e.type_id    = MsgT::type_id();
        e.size       = MsgT::SIZE;
        e.read_only  = read_only;
        e.storage    = storage;
        e.write      = &write_impl<MsgT>;
        e.read       = &read_impl<MsgT>;
        return true;
    }

private:
    struct Entry {
        u8    id        = 0;
        u8    family_id = 0;
        u8    type_id   = 0;
        u8    size      = 0;
        bool  read_only = false;
        void* storage   = nullptr;
        bool  (*write)(void*, const u8*, u8) = nullptr;   // from_bytes → storage
        void  (*read )(const void*, u8*)     = nullptr;   // to_bytes  ← storage
    };

    template<typename MsgT>
    static bool write_impl(void* s, const u8* buf, u8 len) {
        return static_cast<MsgT*>(s)->from_bytes(buf, len);
    }
    template<typename MsgT>
    static void read_impl(const void* s, u8* buf) {
        static_cast<const MsgT*>(s)->to_bytes(buf);
    }

    Entry* find(u8 id) {
        for (u8 i = 0; i < count_; i++)
            if (entries_[i].id == id) return &entries_[i];
        return nullptr;
    }

    void send_value(Entry& e) {
        out_[0] = static_cast<u8>(protocol::OpCode::VALUE);
        out_[1] = e.id;
        out_[2] = e.family_id;
        out_[3] = e.type_id;
        e.read(e.storage, out_ + 4);
        ch_->publish(protocol::PARAM_RES_CHANNEL_ID, out_,
                     static_cast<u8>(4u + e.size));
    }

    void send_err(u8 id, protocol::ErrCode code) {
        out_[0] = static_cast<u8>(protocol::OpCode::ERR);
        out_[1] = id;
        out_[2] = static_cast<u8>(code);
        ch_->publish(protocol::PARAM_RES_CHANNEL_ID, out_, 3);
    }

    void handle(const u8* p, u8 len) {
        if (len < 2) return;
        const auto op = static_cast<protocol::OpCode>(p[0]);
        const u8   id = p[1];
        Entry*      e = find(id);

        switch (op) {
        case protocol::OpCode::GET:
            if (!e) { send_err(id, protocol::ErrCode::UNKNOWN_ID); return; }
            send_value(*e);
            return;

        case protocol::OpCode::SET: {
            if (len < 4)             return;   // [OP][ID][FAM][TYPE] eksik
            if (!e)                { send_err(id, protocol::ErrCode::UNKNOWN_ID);    return; }
            if (e->read_only)      { send_err(id, protocol::ErrCode::READ_ONLY);     return; }
            if (p[2] != e->family_id || p[3] != e->type_id) {
                send_err(id, protocol::ErrCode::TYPE_MISMATCH); return;
            }
            const u8 vlen = static_cast<u8>(len - 4u);
            if (vlen < e->size || !e->write(e->storage, p + 4, vlen)) {
                send_err(id, protocol::ErrCode::BAD_LENGTH); return;
            }
            send_value(*e);   // onay
            return;
        }

        default:
            return;   // VALUE/ERR/bilinmeyen: düğümde yoksay
        }
    }

    // Kanal ChannelCallback imzası: fn(payload, len, ctx)
    static void req_thunk(u8* payload, u8 len, void* ctx) {
        static_cast<Params*>(ctx)->handle(payload, len);
    }

    ChannelT* ch_ = nullptr;
    Entry     entries_[MAX_PARAMS]{};
    u8        count_ = 0;
    u8        out_[4u + MAX_VALUE]{};   // giden VALUE/ERR (kalıcı — reliability için)
};

} // namespace parameters
} // namespace overlays
} // namespace minros
