# minros'un donanım maliyeti

minros'un temel satış argümanı “öngörülebilir bellek ve gecikme” ise bunun
sayısal karşılığı açık olmalıdır. Kütüphane ne kadar RAM ayırır, mesaj gönderirken
ne kadar ek veri taşır ve gecikmeye ne ekler?

Kısa cevap: minros heap kullanmaz. RAM maliyeti derleme sırasında seçilen frame
boyutu ve kanal kapasiteleriyle sabitlenir. Varsayılan, tam özellikli bir
`Node<>` ESP32-S3 üzerinde **1.248 byte RAM** tutar. Dört abonelik, 32-byte frame
ve iki reliable kanal için ayarlanmış `Node<4,32,2>` ise **336 byte** tutar.

Bu değerler kütüphanenin nesne maliyetidir; Arduino, RTOS, UART sürücüsü veya
uygulamanın kendi verileri dahil değildir.

## Bellek neden öngörülebilir?

minros'ta `new`, `malloc`, büyüyen container veya çalışma sırasında oluşan bir
mesaj kuyruğu yoktur. Parser ve framer tamponları ile abonelik ve reliability
tablolarının tamamı nesnenin içinde sabit boyutlu diziler olarak bulunur.

Bunun iki sonucu vardır:

1. Çalışma sırasında heap tükenmesi veya parçalanması oluşmaz.
2. Kullanılabilecek maksimum frame ve kanal sayısı derleme sırasında bellidir.

Bu tasarım kapasite aşımını ortadan kaldırmaz; davranışı görünür hale getirir.
Abonelik tablosu doluysa yeni abonelik, reliable publisher doluysa yeni kanal
oluşturma işlemi `false` döner. Bellek tüketimi ise artmaz.

## Kaç byte RAM?

Aşağıdaki değerler 32-bit pointer kullanan ESP32-S3 ABI'sinde gerçek `sizeof`
ölçümleridir:

| Yapılandırma | Kalıcı RAM |
|---|---:|
| `RawNode<>` | 764 B |
| `Node<>` | **1.248 B** |
| `RawNode<4,32>` | 188 B |
| `Node<4,32,2>` | **336 B** |
| `Node<>::Publisher<Vector3>` | 20 B |

Varsayılan `Node<>` şu kapasitelere sahiptir:

- 16 kullanıcı aboneliği,
- 249-byte DATA alanı,
- 8 reliable publisher kanalı,
- 8 reliable subscriber kanalı.

1.248 byte'ın dağılımı şöyledir:

| Bileşen | RAM |
|---|---:|
| Alım parser'ı | 280 B |
| Gönderim framer'ı | 256 B |
| 17-slot broker | 208 B |
| Dört transport delegate'i | 32 B |
| Reliability tabloları | 272 B |
| 16 tipli abonelik kaydı | 192 B |
| Referans, sayaç ve hizalama | 8 B |
| **Toplam** | **1.248 B** |

Broker'daki on yedinci slot kullanıcıya ait değildir; reliability ACK kanalının
dahili aboneliği için ayrılır.

## RAM nasıl ölçeklenir?

ESP32-S3 üzerinde maliyeti yaklaşık olarak şu kurallarla hesaplamak mümkündür:

- Parser: `align4(MAX_FRAME_DATA + 30)` byte
- Framer: `MAX_FRAME_DATA + 7` byte
- Ham broker aboneliği: 12 byte / slot
- Tipli abonelik: 12 byte / slot
- Reliable publisher: 16 byte / slot
- Reliable subscriber: 16 byte / slot
- Tipli publisher: yaklaşık `align4(MsgT::SIZE + 6)` byte

Frame sınırı iki ayrı tamponu etkiler. `MAX_FRAME_DATA` yaklaşık bir byte
azaltıldığında toplam RAM yaklaşık iki byte azalır: bir byte parser'dan, bir byte
framer'dan. Hizalama yüzünden gerçek sonuç dört-byte basamaklarla değişebilir.

Bu nedenle varsayılan 249-byte kapasiteyi her cihazda kullanmak gereksizdir.
Örneğin `Vector3` mesajı 12 byte'tır. Kanal kimliği eklendiğinde unreliable DATA
13 byte, sequence byte eklendiğinde reliable DATA 14 byte olur. Yalnızca bu tür
küçük mesajları taşıyan bir sistem için 32-byte frame sınırı yeterlidir.

```cpp
// 4 abonelik, 32-byte DATA, 2 reliable pub + 2 reliable sub
minros::Node<4, 32, 2> node;
```

Bu düğümün kalıcı maliyeti **336 byte**'tır.

## Mesaj ve stack maliyeti

Reliable publisher gönderdiği mesajın kopyasını merkezi bir kuyrukta tutmaz.
Yalnızca payload pointer'ı, uzunluk, sequence, zaman ve retry durumunu saklar.
Yüksek seviyeli `Node::Publisher<T>` ise pointer'ın geçerli kalmasını garanti
etmek için kendi içinde `T::SIZE` kadar sabit backing buffer taşır.

Örneğin 12-byte `Vector3` publisher nesnesi hizalamayla beraber 20 byte'tır.
Publisher reliable seçilmese bile nesnenin boyutu değişmez.

Unreliable tipli gönderimde serileştirme için mesaj boyutu kadar geçici stack
buffer'ı açılır. Alım callback'inde deserialize edilen tipli mesaj da stack'te
yaşar. Dolayısıyla kalıcı RAM hesabına ek olarak uygulamanın maksimum mesaj tipi
ve callback çağrı zinciri için stack payı bırakılmalıdır.

Kütüphane tek başına kesin bir “maksimum stack” sayısı veremez; compiler'ın
inline kararları, transport callback'i ve kullanıcı callback'i aynı çağrı
zincirine dahildir. Bu değer hedef firmware üzerinde `-fstack-usage` veya stack
high-water-mark ile doğrulanmalıdır.

## Hat üzerindeki maliyet

Unreliable bir frame şu alanlardan oluşur:

```text
HEADER(4) + LENGTH(1) + CHANNEL(1) + PAYLOAD(P) + CRC(1)
```

Dolayısıyla unreliable mesajın wire maliyeti `P + 7` byte'tır.

Reliable mesaj buna bir sequence byte ekler ve `P + 8` byte taşır. Her reliable
mesaj ayrıca 10-byte ACK frame'i üretir. Retransmit gerekmezse toplam trafik:

- Unreliable: `P + 7` byte
- Reliable: `P + 18` byte

12-byte `Vector3` örneğinde:

| Gönderim | Wire boyutu | Protokol overhead'i |
|---|---:|---:|
| Unreliable | 19 B | 7 B (%36,8) |
| Reliable veri | 20 B | 8 B (%40,0) |
| Reliable veri + ACK | 30 B | 18 B (%60,0) |

Küçük mesajlarda sabit header ve ACK maliyeti belirgindir. Payload büyüdükçe
oran azalır. Paket kaybı halinde reliable frame yeniden gönderildiği için trafik
maliyeti her denemede `P + 8` byte daha artar.

## Gecikme maliyeti

minros gecikmeyi sabit bir milisaniye olarak belirlemez. Transport hızı fiziksel
alt sınırı; `spin_once()` çağrı sıklığı ve uygulama callback'leri gerçek sonucu
belirler.

Kütüphane içindeki iş miktarı sınırlıdır:

- framing ve parsing payload uzunluğuyla `O(P)`,
- CRC hesabı her DATA byte'ı için 8 bit adımı,
- dispatch kayıtlı abonelik sayısıyla `O(MAX_SUBS)`,
- reliable timeout taraması publisher sayısıyla `O(MAX_RELIABLE)` çalışır.

Heap allocation, lock, sanal dispatch veya gizli scheduler yoktur. Bu nedenle
aynı payload ve aynı kanal kapasitesinde kütüphanenin yaptığı işin üst sınırı
bellidir. Ancak uçtan uca gecikmenin üst sınırı yalnızca minros tarafından garanti
edilemez. Ana döngü `spin_once()` çağırmazsa gelen veri beklemeye devam eder.

### UART örneği

8N1 UART'ta bir byte hat üzerinde 10 bit sürer. `Vector3` için teorik, sıfır
yazılım beklemeli iletim süreleri şöyledir:

| İşlem | 115.200 baud | 9.600 baud |
|---|---:|---:|
| Unreliable tek yön, 19 B | 1,65 ms | 19,79 ms |
| Unreliable echo RTT, 38 B | **3,30 ms** | **39,58 ms** |
| Reliable veri, 20 B | 1,74 ms | 20,83 ms |
| ACK, 10 B | 0,87 ms | 10,42 ms |

Bunlar benchmark sonucu değil, hattın matematiksel alt sınırıdır. USB-UART
buffering, loop periyodu, interrupt'lar, RTOS zamanlaması, callback süresi ve
retransmit gerçek süreyi artırır.

Bu yüzden “öngörülebilir gecikme” iddiasının teknik olarak doğru ifadesi şudur:

> minros'un işlem yükü payload uzunluğu ve derleme zamanında belirlenmiş kanal
> sayılarıyla sınırlıdır; gerçek uçtan uca gecikme, transport ve uygulama
> zamanlamasıyla birlikte ölçülür.

Bir ürün metriği verilecekse hedef donanımda baud/transport, payload, loop
periyodu ve kanal sayısı sabitlenerek p50, p95, p99 ve maksimum gecikme ayrıca
raporlanmalıdır.

## Flash maliyeti

minros header-only ve template tabanlıdır. Sadece kullanılan mesaj tipleri ve
kod yolları instantiate edilir; kullanılmayan fonksiyonlar linker garbage
collection ile atılabilir. Bu nedenle bütün projeler için geçerli tek bir flash
sayısı yoktur.

Flash maliyetini belirleyen başlıca seçimler şunlardır:

- `RawNode` veya tipli `Node` kullanılması,
- reliability kod yollarının kullanılması,
- kullanılan mesaj tiplerinin serialize/deserialize fonksiyonları,
- compiler optimizasyonu ve link-time optimization,
- hedef mimarinin instruction set'i.

RAM'den farklı olarak flash maliyeti yalnızca header'lara bakılarak güvenilir
biçimde toplanamaz. Doğru ölçüm, aynı toolchain ve aynı transport ile hazırlanmış
minimal uygulamanın minros'suz ve minros'lu link sonuçları arasındaki farktır.

## Sonuç

minros'un asıl donanım avantajı “çok az kaynak kullanması”ndan önce kaynak
maliyetini baştan sınırlamasıdır:

- Heap maliyeti: **0 byte**
- Varsayılan tam `Node<>`: **1.248 byte kalıcı RAM**
- Küçük `Node<4,32,2>`: **336 byte kalıcı RAM**
- Unreliable wire overhead: **7 byte / frame**
- Reliable wire overhead: **8 byte / data frame + 10 byte ACK**
- İşlem karmaşıklığı: payload ve sabit kanal tablolarıyla sınırlı

Bu sayılar “öngörülebilir bellek” iddiasını doğrudan destekler. Gecikme için ise
kütüphane öngörülebilir bir iş yükü sağlar; milisaniye cinsinden ürün garantisi
transport ve uygulama döngüsü tanımlanmadan verilemez.
