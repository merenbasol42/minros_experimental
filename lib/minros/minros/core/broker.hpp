#pragma once
#include "minros/utils/utils.hpp"

namespace minros::core {

    //
    // Broker: Gelen frame DATA alanını ayrıştırır, CH_ID bazında callback'lere dağıtır.
    //
    // Frame DATA layout:
    //   CH_ID  : 1 byte       (arr[from+0])                     -> kanal kimliği
    //   PAYLOAD: size-2 bytes (arr[from+1] .. arr[from+size-1]) -> asıl veri 
    //
    // TODO: byte hesabına bakmak lazım
    // Template parametre:
    //   MAX_SUBS — maksimum eş zamanlı abonelik sayısı (varsayılan 32)
    //              Küçük MCU'larda 4-8 arası yeterli olabilir; her slot
    //              ARM32'de 12 byte RAM tutar.
    //
    // Kullanım:
    //   Broker<8> broker;
    //   broker.subscribe(ch_id, &MyClass::static_handler, obj);
    //   parser_.set_on_frame_completed(&Broker<8>::frame_cb, &broker);
    //
    template<u8 MAX_SUBS = 16>
    class Broker {
    public:
        // fn(seq, payload, payload_len, ctx)
        using ChannelCallback = utils::delegate<u8, u8*, u8>;

        // CH_ID'ye callback kaydet
        bool subscribe(u8 ch_id, ChannelCallback cb) {
            if (sub_count >= MAX_SUBS) return false;
            subs[sub_count++] = { ch_id, cb };
            return true;
        }

        // Parser'ın on_frame_completed callback'i olarak kullanılacak instance metod
        void on_frame_completed(u8* arr, u8 from, u8 size) {

            u8  ch_id    = arr[from];       // DATA[0] = CH_ID
            u8  seq      = arr[from + 1];   // DATA[1] = SEQ
            u8* payload  = arr + from + 2;  // DATA[2..] = kullanıcı verisi
            u8  pay_len  = size - 2u;       // CH_ID + SEQ çıkarıldı

            for (u8 i = 0; i < sub_count; i++) {
                if (subs[i].ch_id == ch_id && subs[i].cb.is_valid()) {
                    subs[i].cb(seq, payload, pay_len);
                }
            }
        }

        // Parser::set_on_frame_completed() için statik köprü fonksiyonu
        // Örnek: parser_.set_on_frame_completed(&Broker<N>::frame_cb, &broker);
        static void frame_cb(u8* arr, u8 from, u8 size, void* obj) {
            static_cast<Broker*>(obj)->on_frame_completed(arr, from, size);
        }

    private:
        struct Subscription {
            u8      ch_id;
            ChannelCallback cb;
        };

        Subscription subs[MAX_SUBS]{};
        u8      sub_count = 0;
    };

} // namespace minros::core
