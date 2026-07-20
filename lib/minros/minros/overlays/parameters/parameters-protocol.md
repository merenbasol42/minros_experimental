# minros parameters overlay — wire protokolü

Parametre (parameter) overlay'i, düğümlerin çalışma-zamanı yapılandırma
değerlerini (kazanç, eşik, mod vb.) host tarafından **okunup/yazılabilir** hâle
getirir. ROS parametrelerinin gömülü-dostu, string'siz karşılığıdır.

## Tasarım ilkeleri

### 1. Wire'da sayısal ID, isim yok

Parametreler wire üzerinde **sayısal `u8` kimlikle** (`PARAM_ID`) taşınır — tıpkı
kanallar gibi. Gömülü tarafta string tutmak (RAM/flash maliyeti, her erişimde
`strcmp`, değişken boyutlu frame) protokolün sabit-boyut / zero-alloc felsefesine
ters düşer. Model **CANopen object dictionary**'ye benzer: wire'da indeks,
insan-okur isimler yalnızca host/offline tarafta.

- **Wire**: `PARAM_ID` (`u8`) — 0..255, bir MCU için fazlasıyla yeterli.
- **İsimler**: opsiyonel, yalnızca host (minrospy) tarafında `{id: "kp"}` eşlemesi.
  Çekirdek protokol string içermez.

### 2. Değer tipi = mesaj-tip tanımlayıcısı `[FAMILY_ID][TYPE_ID]`

Bir parametrenin değeri, mevcut `interfaces` mesaj sistemindeki bir **mesaj**tir.
Tip, mesaj kimliğiyle taşınır: **`[FAMILY_ID(1)][TYPE_ID(1)]`** (bkz.
`interfaces/msg_base.hpp`). Overlay kendi tip sistemini icat etmez.

- **Primitive** değerler de birer mesajdır: `std_msgs` ailesinde `Float32`
  (`0x00 0x00`), `Int32` (`0x00 0x01`), `Bool` (`0x00 0x07`) …
- **Kompozit** değerler de aynı mekanizmayla desteklenir: `PidGains`
  (`0x00 0x0B`), `geometry_msgs::Vector3` (`0x01 0x00`) …

Kompozit desteğinin asıl gerekçesi **atomiklik**: `PidGains` tek parametre olarak
yazılınca `kp/ki/kd` aynı anda güncellenir; üç ayrı SET'in arasındaki tutarsız
ara durum oluşmaz.

> **Sınır:** yalnızca **sabit boyutlu** tipler. Değişken uzunluklu / dinamik
> değerler (string, dizi) fragmentasyon ve değişken frame demek — protokolün
> felsefesine ters, kapsam dışı.

Değerler wire formatına uygun **little-endian** taşınır; mesajın kendi
`to_bytes()` / `from_bytes()`'ı serileştirmeyi yönetir.

### 3. İki kanal: REQ / RES

Parametre trafiği **iki ayrı kanal** kullanır — istek ve yanıt için birer tane:

| Kanal | Ad | Yön | İçerik |
|------:|-----|-----|--------|
| **247** | `PARAM_REQ` | host → düğüm | GET, SET |
| **246** | `PARAM_RES` | düğüm → host | VALUE, ERR |

Bu ayrım şart, çünkü **reliability overlay'i kanal başına tek publisher varsayar**
(stop-and-wait, kanal başına tek `seq`/ACK uzayı). Host ve düğüm aynı kanalda
publish etseydi iki bağımsız `seq` akışı çakışır, dedup ve ACK bozulurdu. Her
kanalda tek publisher olunca reliability temiz çalışır. Yön kanalla, işlem
op-code ile ayrılır.

Rezerve kanal bloğu (overlay'ler, kullanıcı kanallarıyla çakışmasın diye üstten):

| Kanal | Overlay |
|------:|---------|
| 249 | reliability ACK |
| 248 | logging |
| 247 / 246 | **parameters** (REQ / RES) |

## Wire mesajları

Her frame'in payload'ının ilk baytı **op-code**'dur; logging'deki `FLAGS` gibi
core'un anlamını bilmediği opak bir head önekidir.

```
── PARAM_REQ (CH247, host → düğüm) ────────────────────────────────
GET   : [OP=0x01][PARAM_ID]
        Verilen parametrenin güncel değerini ister.

SET   : [OP=0x02][PARAM_ID][FAMILY_ID][TYPE_ID][msg bytes...]
        Değeri yazar. [FAMILY_ID][TYPE_ID] kayıtlı tiple eşleşmelidir.

── PARAM_RES (CH246, düğüm → host) ────────────────────────────────
VALUE : [OP=0x03][PARAM_ID][FAMILY_ID][TYPE_ID][msg bytes...]
        GET'e yanıt ve/veya başarılı SET onayı.

ERR   : [OP=0x04][PARAM_ID][CODE]
        İşlem reddedildi (aşağıdaki CODE tablosu).
```

### Op-code'lar

| OP | Ad | Kanal |
|----:|-----|-------|
| 0x01 | GET   | PARAM_REQ (247) |
| 0x02 | SET   | PARAM_REQ (247) |
| 0x03 | VALUE | PARAM_RES (246) |
| 0x04 | ERR   | PARAM_RES (246) |

### Hata kodları (ERR CODE)

| CODE | Anlam |
|-----:|-------|
| 0x00 | UNKNOWN_ID     — bu ID'de kayıtlı parametre yok |
| 0x01 | TYPE_MISMATCH  — SET'teki `[FAMILY_ID][TYPE_ID]` kayıtlı tiple uyuşmuyor |
| 0x02 | READ_ONLY      — parametre salt-okunur, yazılamaz |
| 0x03 | BAD_LENGTH     — msg bytes uzunluğu tipin SIZE'ıyla tutarsız |

## Düğüm tarafı davranışı (registry)

Düğüm, sabit boyutlu bir kayıt dizisi (registry) tutar. Her giriş gerçek
değişkene/mesaja bir pointer'dır (kopya yok, alloc yok) ve tipi bilen **tip-silme
(type erasure)** codec'lerini saklar — aynen `node.hpp`'deki `TypedSubEntry`
deseni gibi:

```
ParamEntry {
    u8    id;
    u8    family_id;
    u8    type_id;
    u8    size;          // tipin sabit SIZE'ı
    bool  read_only;
    void* storage;                              // gerçek değişken/mesaj
    void  (*write)(void* storage, const u8*);   // from_bytes → storage
    void  (*read )(const void* storage, u8*);   // to_bytes  ← storage
}
```

`register_param<MsgT>(id, &var, read_only)` çağrısında `family_id/type_id/size`
`MsgT`'den derleme zamanında çıkarılır; `write/read` `MsgT::from_bytes/to_bytes`'a
bağlanır. Primitive'ler de birer `MsgT` (Float32, Int32, …) olduğu için tek yol
hem skaler hem kompoziti kapsar.

- **SET** (CH247): `id` lineer aranır → `[FAMILY_ID][TYPE_ID]` doğrulanır →
  `write` ile `storage`'a yazılır → onay olarak **VALUE** (CH246) yollanır.
- **GET** (CH247): `read` ile `storage` okunur → **VALUE** (CH246) yollanır.
- Uyuşmazlıkta ilgili **ERR** (CH246) yollanır.

## Güvenilirlik

Yapılandırma verisi kaybolmamalı; bu yüzden SET/VALUE **reliability overlay'i**
(CH249 ACK) üzerinden güvenilir taşınır. İki kanallı tasarım tam da bunu mümkün
kılar: `PARAM_REQ`'te tek publisher host, `PARAM_RES`'te tek publisher düğüm →
her kanal reliability'nin tek-publisher sözleşmesine uyar. Overlay yine de
reliability'den bağımsızdır; ikisi de `RawNode`'un pub/sub API'sini kullanan
ayrı katmanlardır.

## Örnek akış

`kp` bir `Float32` (`0x00 0x00`), `pid` bir `PidGains` (`0x00 0x0B`) parametresi:

```
── Float32 oku/yaz ────────────────────────────────────────────────
Host → 247:  GET   id=5                       [01][05]
Düğüm→ 246:  VALUE id=5 Float32 val=1.5        [03][05][00][00][00 00 C0 3F]

Host → 247:  SET   id=5 Float32 val=2.0        [02][05][00][00][00 00 00 40]
Düğüm→ 246:  VALUE id=5 Float32 val=2.0        [03][05][00][00][00 00 00 40]

── PidGains atomik yaz (kp,ki,kd tek seferde) ─────────────────────
Host → 247:  SET   id=7 PidGains ...           [02][07][00][0B][<12 byte>]
Düğüm→ 246:  VALUE id=7 PidGains ...           [03][07][00][0B][<12 byte>]

── Hata ───────────────────────────────────────────────────────────
Host → 247:  SET   id=9 (kayıtsız)             [02][09][00][00][...]
Düğüm→ 246:  ERR   id=9 UNKNOWN_ID             [04][09][00]
```
