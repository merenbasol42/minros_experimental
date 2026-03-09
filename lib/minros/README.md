# minros

`minros`, düşük kaynaklı gömülü sistemler için geliştirdiğimiz, C++17 tabanlı ve header-only bir mesajlaşma kütüphanesidir.  
Amaç, güvenilir ve güvenilmez (best-effort) kanal iletişimini sade bir wire protokolü üzerinde bir araya getirmektir.

## Mimari Hedefler

- Mümkün olduğunca küçük RAM/flash ayak izi (statik buffer, dinamik allocation yok)
- Katmanlı ve değiştirilebilir yapı (transport bağımsız çekirdek)
- Daha öngörülebilir çalışma (loop tabanlı, blocking olmayan akış)
- Basit güvenilirlik (ACK + timeout + retransmit)

## Katmanlar

### 1) Wire Protocol (`core/wireframe.hpp`)

Çerçeve formatını tanımlar:

- `HEADER(4)` + `LEN(1)` + `DATA(LEN)` + `CRC(1)`
- `DATA = CH_ID(1) + SEQ(1) + PAYLOAD(n)`
- `CRC-8/SMBUS` (DATA alanı üzerinden)

Bu katman, platformdan bağımsız protokol tanımını içerir.

### 2) Framer (`core/framer.hpp`)

Uygulama payload’unu wire frame’e dönüştürür:

- `build(ch_id, seq, payload, payload_len)`
- İç buffer’a frame yazar, `data()` / `size()` ile çıkış verir

Temel sorumluluğu *frame üretmek*tir.

### 3) Parser (`core/parser.hpp`)

Byte stream’den frame çıkarır:

- Durum makinesi: `HEADER_WAIT -> LENGTH_WAIT -> DATA_READING -> CRC_WAIT`
- Geçerli frame yakalanınca callback tetikler
- CRC/uzunluk hatalarında resetleyerek akışı sürdürür

Temel sorumluluğu *frame ayrıştırmak*tır.

### 4) Broker (`core/broker.hpp`)

Parse edilen frame’i kanal bazında dağıtır:

- `CH_ID` üzerinden subscriber callback seçimi
- `SEQ` ve `payload` kullanıcı callback’ine iletilir

Temel sorumluluğu *kanal yönlendirme*tir.

### 5) Reliability (`reliability/sequencer.hpp`)

Güvenilir yayın/abonelik davranışını yönetir:

- Publisher tarafı: `seq` üretimi, `ack_pending`, timeout takibi
- Subscriber tarafı: duplicate filtreleme + otomatik ACK gönderimi
- Timeout’ta kullanıcıya `retransmit` callback’i

Temel sorumluluğu *state yönetimi*dir; kullanıcı verisi göndermez.

### 6) Node (`node.hpp`)

Tüm katmanları birleştiren facade:

- Transport callback’leri üzerinden IO
- `spin_once()` içinde: input byte tüketimi + timeout tick
- `create_publisher` / `create_subscription` ile uygulama API’si

`Node`, kütüphanenin pratikteki ana entegrasyon noktasıdır.

## Uçtan Uca Veri Akışı

### Gönderim (Publish Path)

1. Uygulama mesajı (`std_msgs`) byte’a serileştirilir.
2. Reliable ise sequencer’dan `seq` alınır.
3. Framer wire frame üretir.
4. Transport `send_bytes` ile frame gönderilir.

### Alım (Receive Path)

1. Transport’tan gelen byte’lar parser’a verilir.
2. Parser frame tamamlayınca broker callback’i tetikler.
3. Broker `CH_ID` üzerinden ilgili subscriber’a yönlendirir.
4. Reliable abonelikte duplicate kontrolü ve ACK otomatik yapılır.

## Transport Soyutlaması

Kütüphane doğrudan UART/SPI/Ethernet sürücüsüne bağlı değildir.  
Platform entegrasyonu için `Transport` callback’leri atanır:

- `read_bytes`
- `send_bytes`
- `get_size`
- `get_time`

Böylece aynı çekirdek, uygun adaptörlerle farklı donanım/RTOS ortamlarında yeniden kullanılabilir.

## Mesaj Modeli (`std_msgs`)

`std_msgs` katmanı CRTP tabanlı sabit boyutlu mesaj tipleri sunar:

- `Vector3`, `Quaternion`, `Twist`, `PidGains`, primitive tipler
- `to_bytes` / `from_bytes` ile deterministic serialization
- Endianness dönüşümü `utils/endian.hpp` ile yönetilir

## Tasarım Kararları

- `header-only`: entegrasyonu sade tutmak
- `no dynamic memory`: gömülü tarafta daha öngörülebilir davranış hedefi
- `delegate` tabanlı callback: `std::function` maliyetinden kaçınma
- Templated limitler: `MAX_SUBS`, `MAX_DATA`, `MAX_RELIABLE` ile compile-time kaynak kontrolü

## Sınırlar ve Beklentiler

- Thread-safe olma iddiası yoktur; tipik kullanım tek loop/thread’dir.
- Reliability modeli basittir (ACK + retry), tam bir transport protokolü değildir.
- Uygulama seviyesinde kanal planlaması (`CH_ID` ayrımı) kullanıcı sorumluluğundadır.

## Geliştirme Yönü

- Birim test kapsamının genişletilmesi (parser, framer, sequencer edge-case’leri)
- Hata callback’lerinin standartlaştırılması
- Mesaj introspection ve tooling entegrasyonunun olgunlaştırılması
