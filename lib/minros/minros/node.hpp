#pragma once
#include "minros/core/parser.hpp"
#include "minros/core/broker.hpp"
#include "minros/core/framer.hpp"
#include "minros/utils/utils.hpp"


namespace minros {

    struct Transport {
        delegate<void, u8*, u8> send_bytes;
        delegate<void, u8*, u8> read_bytes;
        delegate<u8>            get_size;
        delegate<u32>           get_time;
    };


    // ─────────────────────────────────────────────────────────────────────────
    // Node — saf ham byte datagram API (kanal bazlı publish/subscribe)
    //
    // Reliability'den habersizdir. Güvenilirlik isteyen, bu Node'un public
    // API'sini kullanan reliability::Reliable overlay'ini takar (bkz. reliable.hpp).
    //
    // Template parametreler:
    //   MAX_SUBS       — maksimum abonelik sayısı (varsayılan 16)
    //                    Reliable kullanılıyorsa ACK kanalı için +1 hesaba katılmalı.
    //   MAX_FRAME_DATA — frame DATA alanı maksimum uzunluğu (varsayılan 249)
    //
    // Kullanım:
    //   Node<> node;
    //   node.transport = { ... };
    //
    //   node.subscribe(ch_id, {on_bytes, ctx});   // fn(payload, len, ctx)
    //   node.publish(ch_id, buf, len);
    //
    //   node.spin_once();   // loop() içinde
    //
    // ─────────────────────────────────────────────────────────────────────────

    template<u8 MAX_SUBS       = 16,
             u8 MAX_FRAME_DATA = core::wireframe::MAX_DATA_LEN>
    class Node {
        static_assert(MAX_FRAME_DATA >= core::wireframe::MIN_DATA_LEN &&
                      MAX_FRAME_DATA <= core::wireframe::MAX_DATA_LEN,
                      "MAX_FRAME_DATA aralık dışı");
        static_assert(MAX_SUBS > 0, "MAX_SUBS en az 1 olmalı");

        using BrokerT = core::Broker<MAX_SUBS>;

    public:
        // fn(payload, len, ctx)
        using ChannelCallback = typename BrokerT::ChannelCallback;

        Node() {
            parser_.set_on_frame_completed({&BrokerT::frame_cb, &broker_});
        }

        void spin_once() { feed_parser(); }

        // Ham payload gönder.
        bool publish(u8 ch_id, const u8* payload, u8 len) {
            return raw_publish(ch_id, nullptr, 0, payload, len);
        }

        // Opak HEAD öneki + payload gönder (layered protokoller, örn. reliability seq).
        // Core head'in anlamını bilmez; tele CH_ID + head + payload olarak girer.
        bool publish(u8 ch_id, const u8* head, u8 head_len, const u8* payload, u8 len) {
            return raw_publish(ch_id, head, head_len, payload, len);
        }

        bool subscribe(u8 ch_id, ChannelCallback cb) {
            return broker_.subscribe(ch_id, cb);
        }

        Transport transport;

    private:
        bool raw_publish(u8 ch_id, const u8* head, u8 head_len,
                         const u8* payload, u8 len) {
            if (!transport.send_bytes.is_valid())                       return false;
            if (!framer_.build(ch_id, head, head_len, payload, len))    return false;
            transport.send_bytes(framer_.data(), framer_.size());
            return true;
        }

        void feed_parser() {
            auto w = parser_.write_window();
            u8 n = transport.get_size();
            if (n > w.size) n = w.size;
            transport.read_bytes(w.data, n);
            parser_.commit(n);
        }

        core::Parser<MAX_FRAME_DATA> parser_;
        BrokerT                      broker_;
        core::Framer<MAX_FRAME_DATA> framer_;
    };

} // namespace minros
