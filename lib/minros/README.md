# minros

`minros`, düşük kaynaklı gömülü sistemler için C++17 tabanlı, header-only bir mesajlaşma kütüphanesi geliştirme deneyimidir.
Sade bir wire protokolü üzerinden kanal (CH_ID) tabanlı yayınla/abone ol (pub/sub) iletişimi sağlamayı hedefler.
Güvenilirlik, loglama gibi ek yetenekler çekirdeğe dokunmadan **overlay** olarak eklenir (bkz. [Overlay'ler](#overlayler)).

---

## Tasarım İlkeleri

### Heap yok, sanal dispatch yok

Dinamik bellek tahsisi (`new`, `malloc`) ve sanal fonksiyon (`virtual`) kullanılmaması hedeflenmektedir.
Tüm nesneler derleme zamanında boyutu bilinen statik buffer'larda tutulmakta; bu sayede gömülü sistemlerde bellek tükenmesi ve öngörülemeyen gecikme riskinin önüne geçilmesi amaçlanmaktadır.

### Template parametreleriyle kaynak kontrolü

Statik buffer'lar sabit boyutlu olduğundan farklı projeler farklı miktarda RAM ayırır.
`RawNode`, `Parser`, `Framer`, `Reliable` ve `Node` template parametreleriyle proje ihtiyacına göre ayarlanabilmektedir:

```cpp
// Saf transport: 4 abone, 32 byte'lık frame
minros::RawNode</*MAX_SUBS=*/4, /*MAX_FRAME_DATA=*/32> node;

// Güvenilirlik overlay'i: 2 reliable publisher + 2 reliable subscriber kanalı
minros::overlays::reliability::Reliable</*MAX_PUB=*/2, /*MAX_SUB=*/2> rel{node};

// Tipli yüksek seviye: 4 abone, 32 byte frame, 2 reliable kanal
minros::Node</*MAX_SUBS=*/4, /*MAX_FRAME_DATA=*/32, /*MAX_RELIABLE=*/2> hl;
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
`RawNode::spin_once()` her döngüde çağrılır; gelen baytları işler. Güvenilirlik
kullanılıyorsa ayrıca `Reliable::tick()` çağrılarak timeout/retransmit yürütülür
(`Node::spin_once()` ikisini birlikte yapar).

---

## Mimari

```
┌────────────────────────────────────────────────┐
│  Node (tipli)  ←  RawNode + Reliable sarmalar    │  ← yüksek seviye facade
└───────────────┬────────────────────────────────┘
                │ kullanır (public API)
┌───────────────▼─────────┐     ┌──────────────────────────────┐
│  RawNode (ham byte)     │◄────│  Reliable (overlay)          │
│  publish / subscribe    │     │  seq / ACK / retransmit /    │
│  spin_once()            │     │  duplicate — RawNode'un      │
└────┬──────────────┬─────┘     │  sıradan kullanıcısı (CH249) │
     │              │           └──────────────────────────────┘
  Gönderim       Alım
     │              │
  Framer        Parser  ──→  Broker  ──→  Subscriber callback'leri
     │
  Transport (kullanıcı sağlar)
```

Core (`wireframe`/`framer`/`parser`/`broker`) ve `RawNode` güvenilirlikten habersizdir;
seq diye bir wire alanı yoktur. `Reliable`, RawNode'un public `publish`/`subscribe`
API'sini kullanan bağımsız bir overlay'dir: seq'i payload önekine koyar, ACK'i
normal bir kanaldan (CH249) yollar. `Node` gerektirmez — ham `RawNode` ile de kullanılır.

### Katmanlar

| Dosya | Sorumluluk |
|---|---|
| `core/wireframe.hpp` | Frame formatı sabitleri, CRC-8/SMBUS |
| `core/framer.hpp` | Payload → wire frame dönüşümü (opak head öneki destekli) |
| `core/parser.hpp` | Byte stream → frame (durum makinesi) |
| `core/broker.hpp` | CH_ID bazında callback yönlendirme |
| `raw_node.hpp` | Core'u birleştiren saf ham byte API (`RawNode`) |
| `node.hpp` | `RawNode` + overlay'ler üzerine tipli yüksek seviye sarmalayıcı (`Node`) |
| `interfaces/` | `MsgBase` (CRTP) + mesaj aileleri: `std_msgs/`, `geometry_msgs/` — sabit boyutlu tipler |
| `overlays/` | Çekirdeğe opsiyonel eklenen katmanlar — bkz. [Overlay'ler](#overlayler) |

---

## Overlay'ler

Overlay, çekirdeğe (core + `RawNode`) hiçbir şey eklemeden onun **public
`publish`/`subscribe` API'sini kullanan bağımsız bir katmandır. Ortak özellikleri:

- **Core'u değiştirmez.** Kendi meta verisini (seq, flags gibi) PAYLOAD'ın önüne
  *opak bir önek* olarak koyar; core bunun anlamını bilmez.
- **Kendi rezerve kanalını kullanır.** Kullanıcı kanallarıyla çakışmaması için
  üst kanal bloğu overlay'lere ayrılmıştır.
- **`Node` gerektirmez.** Ham `RawNode` ile de takılabilir; `Node` yalnızca
  bunları tipli API altında bir araya getiren bir kolaylıktır.
- **Bedeli seçime bağlıdır.** Kullanılmayan overlay kod/RAM maliyeti getirmez.

### Rezerve kanal bloğu

| Kanal | Overlay | Durum |
|------:|---------|-------|
| 249 | reliability ACK | mevcut |
| 248 | logging | mevcut |
| 247 / 246 | parameters (REQ / RES) | planlanan |

### reliability — güvenilir teslim

`minros::overlays::reliability::Reliable` (`overlays/reliability/reliable.hpp`)

Stop-and-wait (pencere = 1): kanal başına aynı anda en fazla 1 uçuştaki frame.
Seq'i payload önüne 1 baytlık opak önek olarak koyar, ACK'i CH249'dan yollar;
timeout'ta aynı pointer'dan yeniden gönderir (payload kopyalanmaz). Subscriber
tarafında duplicate filtreler ve ACK'i otomatik üretir.

### logging — best-effort log yayını

`minros::overlays::logging::Logger` (`overlays/logging/logger.hpp`), `Node` üzerinden
`log_info` / `log_error` vb. ile kullanılır.

CH248'den yalnızca **yayın** yapar; zero-buffer'dır (string literal flash'ta kalır).
Eşik altındaki seviyeler wire'a hiç dokunmadan döner, uzun mesaj otomatik parçalanır.
Log **almak** için host tarafında bir sink kullanılır (minrospy Python sink).

### parameters — çalışma-zamanı yapılandırma (planlanan)

Düğümlerin parametrelerini (kazanç, eşik, mod) host tarafından okunup/yazılabilir
kılar. İki kanal (REQ/RES), sayısal `[FAMILY_ID][TYPE_ID]` tip tanımlayıcısı ve
sabit-boyutlu değerler üzerine kurgulanır. Ayrıntı: `overlays/parameters/`.

---

## Wire Protokolü

```
┌──────────┬─────┬──────────────────────┬─────┐
│ HEADER   │ LEN │ DATA                 │ CRC │
│ 4 byte   │ 1 B │ CH_ID(1) PAYLOAD(n)  │ 1 B │
└──────────┴─────┴──────────────────────┴─────┘
```

- **HEADER**: `{0x6D, 0x72, 0x6F, 0x73}` (`mros`) — senkronizasyon
- **LEN**: DATA uzunluğu (2–249)
- **CRC**: CRC-8/SMBUS, DATA alanının tamamı üzerinden (CH_ID + PAYLOAD)
- Wire formatı little-endian; host dönüşümü `utils/endian.hpp` ile yönetilmektedir

> Core SEQ bilmez. Güvenilir mesajlarda `Reliable`, PAYLOAD'ın önüne 1 baytlık seq
> öneki koyar (`[SEQ][user bytes]`); ACK frame'leri CH249'da `[RESP][CH][SEQ]` taşır.
> Bu, core için opak veridir.

---

## Veri Akışı

### Gönderim

1. Uygulama mesajı (`std_msgs`) `to_bytes()` ile serileştirilir.
2. Reliable ise `Reliable::publish()` seq'i payload önüne ekler ve payload'ın
   **pointer'ını tutar** (kopya yok); ACK bekleniyorsa `false` döner (`can_send`).
3. `Framer::build()` wire frame üretir.
4. `Transport::send_bytes` frame'i iletir.
5. ACK gelmezse `Reliable::tick()` aynı pointer'dan otomatik yeniden gönderir.

### Alım

1. `spin_once()` → `Transport::get_size` + `Transport::read_bytes` ile baytlar parser buffer'ına alınır (zero-copy).
2. `Parser::commit(n)` durum makinesini çalıştırır; frame tamamlanınca broker tetiklenir.
3. `Broker`, `CH_ID` üzerinden ilgili subscriber callback'ini çağırır.
4. Reliable abonelikte `Reliable` wrapper'ı seq önekini ayıklar, duplicate kontrolü
   yapar ve ACK gönderimini otomatik gerçekleştirir.

---

## Kullanım

İki kullanım seviyesi vardır. `Node` tipli mesajlarla (serialize/deserialize otomatik)
çalışan yüksek seviye sarmalayıcıdır; `RawNode` ise doğrudan ham byte ile çalışan çekirdektir.
`Node` kullanmak zorunlu değildir — `RawNode` tek başına oluşturulabilir.

### Yüksek seviye — `Node` (tipli)

```cpp
#include <minros/node.hpp>
#include <minros/interfaces/geometry_msgs/twist.hpp>

minros::Node<> node;

// Transport bağla
node.transport = { ... };

// Reliable publisher — retransmit otonomdur, callback gerekmez.
// Publisher mesajı kendi buffer'ında tutar (retransmit backing).
auto cmd = node.create_publisher<minros::interfaces::geometry_msgs::Twist>(CH_CMD, /*reliable=*/true);

// Reliable subscriber — callback doğrudan tipli mesaj alır
// void on_cmd_received(const minros::interfaces::geometry_msgs::Twist& msg, void* ctx)
node.create_subscription<minros::interfaces::geometry_msgs::Twist>(
    CH_CMD, { on_cmd_received, nullptr }, /*reliable=*/true
);

// Loop içinde
void loop() {
    node.spin_once();                       // parser + reliable tick birlikte
    if (!cmd.publish(twist_msg)) {
        // önceki mesaj hâlâ uçuşta (ACK bekleniyor) — sonra dene
    }
}
```

### Düşük seviye — `RawNode` (ham byte)

`RawNode` tek başına yeterlidir; tipli katmana ihtiyaç yoksa doğrudan kullanılabilir.
Serileştirme/deserileştirme tamamen kullanıcıya aittir.

```cpp
#include <minros/raw_node.hpp>
#include <minros/overlays/reliability/reliable.hpp>

minros::RawNode<> node;
minros::overlays::reliability::Reliable rel{node};   // overlay'i node'a tak (CTAD)

// Transport bağla
node.transport = { ... };

// Unreliable subscriber — callback ham payload alır
// void on_bytes(u8* payload, u8 len, void* ctx)
node.subscribe(CH_TELEM, { on_bytes, nullptr });

// Reliable subscriber (duplicate filtre + otomatik ACK)
// callback seq önekı ayıklanmış kullanıcı verisini alır
rel.subscribe(CH_CMD, { on_cmd_bytes, nullptr });

// Reliable publisher için: buffer ACK'e kadar SABİT kalmalı (Reliable pointer tutar)
static u8 cmd_tx[4];

// Loop içinde
void loop() {
    node.spin_once();   // gelen baytlar
    rel.tick();         // timeout/retransmit

    u8 payload[4] = { 1, 2, 3, 4 };
    node.publish(CH_TELEM, payload, sizeof(payload));   // unreliable

    if (rel.can_send(CH_CMD)) {                         // önceki ACK'lendi mi?
        memcpy(cmd_tx, payload, 4);
        rel.publish(CH_CMD, cmd_tx, sizeof(cmd_tx));    // reliable
    }
}
```

> `Node`, içte bir `RawNode` + `Reliable` tutar ve `to_bytes()` / `from_bytes()`
> çağrılarını bu ham API üzerine ekler; reliable publisher buffer'ını da kendi
> yönetir. İki seviye aynı wire protokolünü paylaşır; karışık kullanılabilir.

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

- Birim test kapsamının genişletilmesi (parser edge-case'leri, reliable seq wraparound)
- Hata callback'lerinin standartlaştırılması
