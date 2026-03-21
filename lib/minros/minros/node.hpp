#pragma once
#include "minros/core/parser.hpp"
#include "minros/core/broker.hpp"
#include "minros/core/framer.hpp"
#include "minros/reliability/sequencer.hpp"
#include "minros/utils/utils.hpp"


namespace minros {

    struct Transport {
        utils::delegate<void, u8*, u8> send_bytes;
        utils::delegate<void, u8*, u8> read_bytes;
        utils::delegate<u8>            get_size;
        utils::delegate<u32>           get_time;
    };


    // ─────────────────────────────────────────────────────────────────────────
    // Node
    //
    // Template parametreler:
    //   MAX_CH         — maksimum kanal sayısı (varsayılan 32)
    //   MAX_SUBS       — broker'a kayıt edilebilecek maksimum abonelik (varsayılan 16)
    //   MAX_FRAME_DATA — frame DATA alanı maksimum uzunluğu (varsayılan 249)
    //   MAX_RELIABLE   — güvenilir publisher + subscriber başına maksimum kanal (varsayılan 8)
    //
    // Kullanım özeti:
    //   Node<> node;
    //   node.transport = { ... };
    //
    //   // Non-reliable subscriber
    //   node.subscribe(ch_id, {on_data, ctx});
    //
    //   // Reliable subscriber
    //   node.create_subscription<Twist>(ch_id, {on_cmd, ctx}, true);
    //
    //   // Non-reliable publisher
    //   auto pub = node.create_publisher<Vector3>(ch_id, "imu_accel");
    //   pub.publish(vec);
    //
    //   // Reliable publisher
    //   auto cmd = node.create_publisher<Twist>(ch_id, "cmd_vel", true, {on_retransmit, ctx});
    //   cmd.publish(msg);
    //
    //   // Loop içinde
    //   node.spin_once();
    //
    // ─────────────────────────────────────────────────────────────────────────

    template<u8 MAX_CH         = 32,
             u8 MAX_SUBS       = 16,
             u8 MAX_FRAME_DATA = core::wireframe::MAX_DATA_LEN,
             u8 MAX_RELIABLE   = 8>
    class Node {
        static_assert(MAX_FRAME_DATA >= core::wireframe::MIN_DATA_LEN &&
                      MAX_FRAME_DATA <= core::wireframe::MAX_DATA_LEN,
                      "MAX_FRAME_DATA aralık dışı");

    public:
        using ChannelCallback    = typename core::Broker<MAX_SUBS>::ChannelCallback;
        using ParseErrorCallback = typename core::Parser<MAX_FRAME_DATA>::ErrorCallback;
        using RetransmitCallback = typename reliability::Sequencer<MAX_RELIABLE, MAX_RELIABLE>::RetransmitCallback;

        // ── Publisher ────────────────────────────────────────────────────────
        //
        // MsgT tipini bilen ince sarmalayıcı. Veri tutmaz.
        // Reliable modda publish akışı:
        //   1. sequencer_.acquire_seq()   — seq al; ACK bekliyorsa false döner
        //   2. node_->publish(seq)     — frame gönder
        //   3. sequencer_.notify_sent() — timeout sayacını başlat
        //
        template<typename MsgT>
        struct Publisher {
            Publisher() = default;

            bool publish(const MsgT& msg) {
                if (!node_) return false;

                u8 buf[MsgT::SIZE];
                msg.to_bytes(buf);

                if (!reliable_) {
                    return node_->publish(ch_id_, buf, MsgT::SIZE);

                } else {
                    // Reliable: seq al — timeout sayacı acquire_seq içinde başlar
                    auto seq_ret = node_->sequencer_.acquire_seq(ch_id_, node_->transport.get_time());
                    if (!seq_ret.success) return false;
    
                    return node_->publish(ch_id_, buf, MsgT::SIZE, seq_ret.seq);
                }
            }

            bool is_valid() {
                return node_;
            }

        private:
            friend class Node;
            Publisher(Node* n, u8 id, bool reliable)
                : node_(n), ch_id_(id), reliable_(reliable) {}
            Node* node_     = nullptr;
            u8    ch_id_    = 0;
            bool  reliable_ = false;
        };


        // ── Kurucu ───────────────────────────────────────────────────────────
        Node() {
            parser_.set_on_frame_completed({&core::Broker<MAX_SUBS>::frame_cb, &broker_});

            // Sequencer sadece ACK frame'leri gönderebilir — kullanıcı verisi değil.
            sequencer_.set_ack_sender({&Node::ack_publish_cb, this});
            broker_.subscribe(reliability::protocol::ACK_CHANNEL_ID,
                              sequencer_.ack_callback());
        }


        // ── Temel API ────────────────────────────────────────────────────────

        // Periyodik işlemler — loop() içinde çağır
        void spin_once() {
            feed_parser();
            sequencer_.tick(transport.get_time());
        }

        bool subscribe(u8 ch_id, ChannelCallback cb) {
            return broker_.subscribe(ch_id, cb);
        }

        // reliable=true: Sequencer wrapper → duplicate tespiti + otomatik ACK
        template<typename MsgT>
        bool create_subscription(u8 ch_id, ChannelCallback cb, bool reliable = false) {
            if (reliable) {
                ChannelCallback wrapped = sequencer_.make_reliable_sub(ch_id, cb);
                if (!wrapped.is_valid()) return false;
                return broker_.subscribe(ch_id, wrapped);
            }
            return broker_.subscribe(ch_id, cb);
        }

        // reliable=true ise retransmit_cb zorunlu:
        //   fn(ch_id, seq, ctx) — timeout'ta çağrılır, kullanıcı pub.publish(msg) çağırır
        template<typename MsgT>
        Publisher<MsgT> create_publisher(
            u8                 ch_id,
            const char*        ch_name,
            bool               reliable      = false,
            RetransmitCallback retransmit_cb = {}
        ) {
            if (reliable) {
                if(!sequencer_.register_pub(ch_id, retransmit_cb)) return {};
            }
            return Publisher<MsgT>(this, ch_id, reliable);
        }

        bool publish(u8 ch_id, u8* payload, u8 len, u8 seq = 0x00) {
            if (!transport.send_bytes.is_valid())          return false;
            if (!framer_.build(ch_id, seq, payload, len))  return false;
            transport.send_bytes(framer_.data(), framer_.size());
            return true;
        }

        Transport transport;

    private:
        core::Parser<MAX_FRAME_DATA>         parser_;
        core::Broker<MAX_SUBS>               broker_;
        core::Framer<MAX_FRAME_DATA>         framer_;
        reliability::Sequencer<MAX_RELIABLE, MAX_RELIABLE> sequencer_;

        void feed_parser() {
            auto w = parser_.write_window();
            u8 n = transport.get_size();
            if (n > w.size) n = w.size;
            transport.read_bytes(w.data, n);
            parser_.commit(n);
        }

        // Sequencer'ın ACK frame'lerini göndermesi için köprü.
        // Sequencer send_ack'i fn(ch_id, seq, payload, len) ile çağırır.
        static void ack_publish_cb(u8 ch_id, u8 seq, u8* payload, u8 len, void* ctx) {
            static_cast<Node*>(ctx)->publish(ch_id, payload, len, seq);
        }
    };

} // namespace minros
