#pragma once
#include "minros/node.hpp"
#include "minros/reliability/reliable.hpp"

#include <cstring>


namespace minros {

    // ─────────────────────────────────────────────────────────────────────────
    // NodeHL — yüksek seviye tipli API (Node + reliability::Reliable sarmalayıcı)
    //
    // Template parametreler:
    //   MAX_SUBS       — maksimum abonelik sayısı (varsayılan 16)
    //                    Dahili broker'a ACK kanalı için +1 slot verilir.
    //   MAX_FRAME_DATA — frame DATA alanı maksimum uzunluğu (varsayılan 249)
    //   MAX_RELIABLE   — güvenilir publisher/subscriber başına maksimum kanal (varsayılan 8)
    //
    // Kullanım:
    //   NodeHL<> node;
    //   node.transport = { ... };
    //
    //   // Publisher
    //   auto pub = node.create_publisher<Twist>(ch_id);
    //   pub.publish(msg);
    //
    //   // Subscriber — callback doğrudan tipli mesaj alır
    //   node.create_subscription<Twist>(ch_id, {on_cmd, ctx});
    //   // fn imzası: void on_cmd(const Twist& msg, void* ctx)
    //
    //   // Güvenilir publisher — buffer'ı Publisher kendi içinde tutar
    //   auto pub = node.create_publisher<Twist>(ch_id, /*reliable=*/true);
    //   if (!pub.publish(msg)) { /* önceki hâlâ uçuşta, sonra dene */ }
    //
    //   // Güvenilir subscriber
    //   node.create_subscription<Twist>(ch_id, {on_cmd, ctx}, /*reliable=*/true);
    //
    //   node.spin_once();   // loop() içinde
    //
    // ─────────────────────────────────────────────────────────────────────────

    template<u8 MAX_SUBS       = 16,
             u8 MAX_FRAME_DATA = core::wireframe::MAX_DATA_LEN,
             u8 MAX_RELIABLE   = 8>
    class NodeHL {
        static_assert(MAX_SUBS > 0,   "MAX_SUBS en az 1 olmalı");
        static_assert(MAX_SUBS < 255, "MAX_SUBS 255'ten küçük olmalı (ACK için +1 slot)");

        // ACK kanalı dahili broker'da +1 slot tüketir.
        using NodeT     = Node<static_cast<u8>(MAX_SUBS + 1u), MAX_FRAME_DATA>;
        using ReliableT = reliability::Reliable<NodeT, MAX_RELIABLE, MAX_RELIABLE>;

    public:
        // TypedCallback<MsgT> = delegate<void, const MsgT&>
        // fn imzası: void fn(const MsgT& msg, void* ctx)
        template<typename MsgT>
        using TypedCallback = delegate<void, const MsgT&>;


        // ── Publisher ─────────────────────────────────────────────────────
        //
        // create_publisher<MsgT>() tarafından döndürülür.
        // Reliable ise mesajı serileştirip kendi buf_'unda tutar (retransmit backing);
        // Reliable bu buffer'ın pointer'ını tutar. Bu yüzden:
        //   • kopyalanamaz (kopya farklı adres → dangling),
        //   • taşınabilir, AMA yalnızca uçuşta mesaj YOKKEN (örn. setup'ta atama).
        //
        template<typename MsgT>
        struct Publisher {
            Publisher() = default;

            Publisher(const Publisher&)            = delete;
            Publisher& operator=(const Publisher&) = delete;
            Publisher(Publisher&&)                 = default;
            Publisher& operator=(Publisher&&)      = default;

            bool publish(const MsgT& msg) {
                if (!hl_) return false;

                if (reliable_) {
                    if (!hl_->reliable_.can_send(ch_id_)) return false;  // uçuşta
                    msg.to_bytes(buf_);                                  // kalıcı backing
                    return hl_->reliable_.publish(ch_id_, buf_, MsgT::SIZE);
                }

                u8 buf[MsgT::SIZE];                                      // unreliable: stack yeterli
                msg.to_bytes(buf);
                return hl_->node_.publish(ch_id_, buf, MsgT::SIZE);
            }

            bool is_valid() const { return hl_ != nullptr; }

        private:
            friend class NodeHL;
            Publisher(NodeHL* hl, u8 id, bool reliable)
                : hl_(hl), ch_id_(id), reliable_(reliable) {}

            NodeHL* hl_       = nullptr;
            u8      ch_id_    = 0;
            bool    reliable_ = false;
            u8      buf_[MsgT::SIZE]{};   // reliable retransmit için kalıcı backing
        };


        // ── Kurucu ───────────────────────────────────────────────────────
        NodeHL() : transport(node_.transport), reliable_(node_) {}

        NodeHL(const NodeHL&)            = delete;
        NodeHL& operator=(const NodeHL&) = delete;


        // ── API ───────────────────────────────────────────────────────────

        // node.transport = { ... }; ile doğrudan atama çalışır
        Transport& transport;

        void spin_once() {
            node_.spin_once();
            reliable_.tick();
        }

        // reliable=true ise güvenilir publisher kanalı kaydedilir.
        template<typename MsgT>
        Publisher<MsgT> create_publisher(u8 ch_id, bool reliable = false) {
            if (reliable && !reliable_.register_pub(ch_id)) return {};
            return Publisher<MsgT>(this, ch_id, reliable);
        }

        template<typename MsgT>
        bool create_subscription(u8 ch_id, TypedCallback<MsgT> cb, bool reliable = false) {
            if (!cb.is_valid())               return false;
            if (typed_sub_count_ >= MAX_SUBS) return false;

            TypedSubEntry& e = typed_subs_[typed_sub_count_++];
            e.dispatch = &TypedSubEntry::template make_dispatch<MsgT>;

            // Tipli fonksiyon pointer'ını void* olarak sakla.
            // sizeof(fn*) == sizeof(void*) tüm hedef platformlarda (ARM Cortex-M, ESP32, x86).
            auto fn = cb.raw_fn();
            memcpy(&e.fn, &fn, sizeof(e.fn));
            e.ctx = cb.raw_ctx();

            if (reliable)
                return reliable_.subscribe(ch_id, {&TypedSubEntry::raw_adapter, &e});
            return node_.subscribe(ch_id, {&TypedSubEntry::raw_adapter, &e});
        }

    private:
        // ── Tip silme (type erasure) — abonelik için ─────────────────────
        //
        // Her TypedSubEntry, MsgT tipini bilen bir dispatch fonksiyonu saklar.
        // Broker/Reliable: raw byte → raw_adapter → dispatch → deserialize → tipli callback.
        //
        struct TypedSubEntry {
            void (*dispatch)(const u8* payload, u8 len, void* fn, void* ctx) = nullptr;
            void* fn  = nullptr;
            void* ctx = nullptr;

            template<typename MsgT>
            static void make_dispatch(const u8* payload, u8 len, void* fn, void* ctx) {
                MsgT msg;
                if (!msg.from_bytes(payload, len)) return;
                void (*typed_fn)(const MsgT&, void*);
                memcpy(&typed_fn, &fn, sizeof(typed_fn));
                typed_fn(msg, ctx);
            }

            // Node/Reliable ChannelCallback imzası: fn(payload, len, ctx)
            static void raw_adapter(u8* payload, u8 len, void* self) {
                auto* e = static_cast<TypedSubEntry*>(self);
                if (e->dispatch) e->dispatch(payload, len, e->fn, e->ctx);
            }
        };

        NodeT         node_;
        ReliableT     reliable_;
        TypedSubEntry typed_subs_[MAX_SUBS]{};
        u8            typed_sub_count_ = 0;
    };

} // namespace minros
