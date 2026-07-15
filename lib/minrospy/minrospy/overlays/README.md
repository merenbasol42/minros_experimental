# minros overlays

**Overlay**, minros çekirdeğinin (core) *üzerine* oturan ama çekirdeğe hiçbir şey
eklemeyen bağımsız bir protokol katmanıdır. Her overlay yalnızca `RawNode`'un
public `publish`/`subscribe` API'sini kullanan sıradan bir "kullanıcı"dır —
framer, parser, broker ve wire formatı overlay'lerden habersizdir.

Bu klasördeki katmanlar:

| Overlay | Kanal | Ne yapar |
|---|---|---|
| [`reliability`](reliability/) | **249** (ACK) | ACK + retransmit ile güvenilir teslim (stop-and-wait, window=1) |
| [`logging`](logging/) | **248** | Seviyeli, best-effort log yayını + parça birleştirme (sink) |

Namespace / paket:
- C++ : `minros::overlays::reliability`, `minros::overlays::logging`
- Python : `minrospy.overlays.reliability`, `minrospy.overlays.logging`

---

## Overlay sözleşmesi

Bir katmanın "overlay" sayılması için uyduğu ortak kurallar:

### 1. Core'a dokunma
Overlay, `RawNode`'un public API'si dışında hiçbir çekirdek yapısını değiştirmez.
Böylece çekirdek küçük ve tek sorumluluklu kalır; overlay olmadan da RawNode tek
başına çalışır. `Node` (tipli yüksek seviye facade) overlay'leri sarmalar ama
onlara mecbur değildir.

### 2. Rezerve kanal + opak head öneki
Overlay'in kendi meta verisi, kullanıcı payload'ının önüne **opak bir head öneki**
olarak konur; core bu öneki bilmez, yalnızca overlay'in alıcı tarafı ayıklar.
Overlay'ler arası kanal çakışmasını önlemek için üst blok rezerve edilmiştir:

```
Rezerve protokol kanal bloğu
    249 = reliability ACK
    248 = logging
    ...  (yeni overlay'ler buradan aşağı doğru)
```

Kullanıcı kanalları bu bloğa girmemelidir.

### 3. Statik + kullanılmazsa sıfır maliyet
Overlay'ler template parametreleriyle (C++) / kurucu argümanlarıyla (Python)
boyutlanır; heap ve sanal dispatch kullanmaz. Kullanılmayan bir overlay ideal
olarak RAM/flash maliyeti getirmez (ör. `logging::Logger` yayıncısı zero-buffer;
reassembly buffer'ı yalnızca `LogSink`'te).

### 4. İki dilde simetri
C++ (`minros`) ve Python (`minrospy`) portları aynı wire formatını üretir. Bir
overlay eklerken iki tarafı da birlikte güncelle; `conformance/` vektörleriyle
uyum doğrulanabilir.

---

## Katmanların özeti

### reliability
Publisher ACK gelene kadar yeni mesaj göndermez (`can_send` false); ACK gelmezse
`tick()` timeout'ta payload'ı otonom yeniden gönderir (kopya tutar / pointer
tutar, retransmit callback'i yoktur). Subscriber tarafı dedup + otomatik ACK
yapar. Head öneki: 1 baytlık `SEQ`. Ayrıntı: [reliability/](reliability/).

### logging
Best-effort (unreliable) seviyeli log. Kaynakta `min_level` filtresi eşik altı
çağrıları wire'a hiç dokundurmaz. Uzun satır, küçük frame'lerde otomatik
parçalanır; sink `SEQ4` sürekliliğiyle kayıp parçayı tespit edip bozuk satır
üretmez. Head öneki: 1 baytlık `FLAGS` (`LAST | LEVEL | SEQ4`). Ayrıntı:
[logging/](logging/).

---

## Yeni bir overlay eklemek

1. Rezerve bloktan bir kanal seç (248'in altından) ve buradaki tabloya ekle.
2. Meta verini **opak head öneki** olarak tasarla; core'a alan ekleme.
3. `RawNode`'a takılan bağımsız bir sınıf yaz (`publish`/`subscribe` kullanır).
   Kullanılmadığında maliyet getirmeyecek şekilde publisher/sink'i ayır.
4. C++ ve Python portlarını birlikte yaz; aynı wire formatını üret.
5. İstersen `Node` facade'ına ince bir API ekle (reliability/logging gibi).
