# minros

`minros`, düşük kaynaklı gömülü sistemler için C++17 tabanlı, header-only bir mesajlaşma kütüphanesi geliştirme deneyimidir.
Güvenilir (ACK + retransmit) ve güvenilmez (best-effort) kanal iletişimini sade bir wire protokolü üzerinde bir araya getirmeyi hedeflemektedir.

---

## Tasarım İlkeleri

### Heap yok, sanal dispatch yok

Dinamik bellek tahsisi (`new`, `malloc`) ve sanal fonksiyon (`virtual`) kullanılmaması hedeflenmektedir.
Tüm nesneler derleme zamanında boyutu bilinen statik buffer'larda tutulmakta; bu sayede gömülü sistemlerde bellek tükenmesi ve öngörülemeyen gecikme riskinin önüne geçilmesi amaçlanmaktadır.

### Template parametreleriyle kaynak kontrolü

Statik buffer'lar sabit boyutlu olduğundan farklı projeler farklı miktarda RAM ayırır.
`Node`, `Parser`, `Sequencer` ve `Framer` template parametreleriyle proje ihtiyacına göre ayarlanabilmektedir:

```cpp
// Küçük bir düğüm: 4 abone, 32 byte'lık frame, 2 reliable kanal
minros::Node</*MAX_SUBS=*/4, /*MAX_FRAME_DATA=*/32, /*MAX_RELIABLE=*/2> node;
```

### Donanımdan bağımsızlık

Kütüphane içinde donanıma özgü herhangi bir çağrı bulunmaması hedeflenmektedir.
IO işlevleri kullanıcı tarafından dört callback ile sağlanır; kütüphane bunları transport olarak kullanır:

```cpp
node.transport = {
    .send_bytes = { my_uart_write,     ctx },
    .read_bytes = { my_uart_read,      ctx },
    .get_size   = { my_uart_available, ctx },
    .get_time   = { my_millis,         ctx },
};
```

Şu an UART üzerinde denenmekte olup aynı çekirdek kodun transport callback'leri değiştirilerek UDP gibi farklı ortamlara da taşınabilmesi amaçlanmaktadır.

### Basitlik

Arayüzün kasıtlı olarak küçük tutulması hedeflenmektedir.
`Node::spin_once()` her döngüde çağrılır; gelen baytları işler ve timeout tick'lerini atar.

---

## Mimari

```
┌─────────────────────────────────┐
│             Node                │  ← Kullanıcı arayüzü (facade)
│  create_publisher / subscribe   │
│  spin_once()                    │
└────┬──────────────┬─────────────┘
     │              │
  Gönderim       Alım
     │              │
  Framer        Parser  ──→  Broker  ──→  Subscriber callback'leri
     │
  Transport (kullanıcı sağlar)
     │
  Sequencer  ←──────────────────────────  ACK kanalı (CH249)
```

### Katmanlar

| Dosya | Sorumluluk |
|---|---|
| `core/wireframe.hpp` | Frame formatı sabitleri, CRC-8/SMBUS |
| `core/framer.hpp` | Payload → wire frame dönüşümü |
| `core/parser.hpp` | Byte stream → frame (durum makinesi) |
| `core/broker.hpp` | CH_ID bazında callback yönlendirme |
| `reliability/sequencer.hpp` | seq üretimi, ACK takibi, duplicate filtre |
| `node.hpp` | Tüm katmanları birleştiren facade |
| `std_msgs/` | CRTP tabanlı sabit boyutlu mesaj tipleri |

---

## Wire Protokolü

```
┌──────────┬─────┬────────────────────────────┬─────┐
│ HEADER   │ LEN │ DATA                       │ CRC │
│ 4 byte   │ 1 B │ CH_ID(1) SEQ(1) PAYLOAD(n) │ 1 B │
└──────────┴─────┴────────────────────────────┴─────┘
```

- **HEADER**: `{0x6D, 0x72, 0x6F, 0x73}` (`mros`) — senkronizasyon
- **LEN**: DATA uzunluğu (3–249)
- **CRC**: CRC-8/SMBUS, DATA alanının tamamı üzerinden (CH_ID + SEQ + PAYLOAD)
- Wire formatı little-endian; host dönüşümü `utils/endian.hpp` ile yönetilmektedir

---

## Veri Akışı

### Gönderim

1. Uygulama mesajı (`std_msgs`) `to_bytes()` ile serileştirilir.
2. Reliable ise `Sequencer::acquire_seq()` ile seq alınır.
3. `Framer::build()` wire frame üretir.
4. `Transport::send_bytes` frame'i iletir.

### Alım

1. `spin_once()` → `Transport::get_size` + `Transport::read_bytes` ile baytlar parser buffer'ına alınır (zero-copy).
2. `Parser::commit(n)` durum makinesini çalıştırır; frame tamamlanınca broker tetiklenir.
3. `Broker`, `CH_ID` üzerinden ilgili subscriber callback'ini çağırır.
4. Reliable abonelikte duplicate kontrolü ve ACK gönderimi otomatik olarak gerçekleştirilmektedir.

---

## Kullanım

```cpp
#include <minros/node.hpp>
#include <minros/std_msgs/twist.hpp>

minros::Node<> node;

// Transport bağla
node.transport = { ... };

// Reliable publisher
auto cmd = node.create_publisher<minros::std_msgs::Twist>(
    CH_CMD, "cmd_vel", /*reliable=*/true, { on_retransmit, nullptr }
);

// Reliable subscriber
node.create_subscription<minros::std_msgs::Twist>(
    CH_CMD, { on_cmd_received, nullptr }, /*reliable=*/true
);

// Loop içinde
void loop() {
    node.spin_once();
    cmd.publish(twist_msg);
}
```

---

## Sınırlar

- Thread-safe değildir; tipik kullanım tek döngü/thread içindir.
- Reliability modeli basittir: ACK + retry. Tam bir transport protokolü (akış kontrolü, sıralama garantisi) değildir.
- CH_ID ayrımı ve kanal planlaması kullanıcı sorumluluğundadır.
- Parser buffer'ı bir frame'den büyük olamaz; çok büyük payload'lar `MAX_FRAME_DATA` ile kısıtlanmalıdır.

---

## Yol Haritası

### Opsiyonel keşif katmanı

İkili (binary) protokol olmasına karşın birden fazla cihazın birbirini tanıyıp mesajlaşabilmesi için opsiyonel bir keşif mekanizması eklemek hedeflenmektedir.
Her düğümün yayınladığı/abone olduğu kanalları ve mesaj tiplerini duyurabilmesi; bir merkez düğümün (MQTT broker benzeri) bu bilgileri toplayarak çok-noktaya yönlendirme yapabilmesi planlanmaktadır.
Bu özelliğin mevcut çekirdeğe dokunmadan opsiyonel olarak eklenmesi amaçlanmaktadır.

### Diğer

- Birim test kapsamının genişletilmesi (parser edge-case'leri, sequencer wraparound)
- Hata callback'lerinin standartlaştırılması
