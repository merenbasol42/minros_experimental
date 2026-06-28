# conformance — wire protokolü uyumluluk vektörleri

`minros` (C++, gömülü) ve `minrospy` (saf Python) **aynı wire protokolünün iki
ayrı implementasyonudur**. İkisi senkron kalmazsa cihaz ile host birbirini
anlayamaz. Bu dizin, ikisini de aynı **tarafsız altın vektörlere** karşı sınayarak
bu drift'i otomatik yakalar.

## Neden tarafsız?

Vektörler ne minros'tan ne de minrospy'den türetilir. [generate.py](generate.py)
beklenen byte'ları yalnızca **protokol spec'inden** ve Python standart
kütüphanesinden (`struct` + spec'e göre yazılmış CRC) hesaplar. Anchor olarak
CRC-8/SMBUS'un belgelenmiş `"123456789" → 0xF4` kontrol değeri de vektörlerde
bulunur; oracle'ın kendi kendini değil gerçek standardı doğruladığını gösterir.

Böylece bir taraf yanlışsa (ya da ikisi farklı yanlışsa) test kırılır — iki
implementasyon aynı hatayı paylaşsa bile oracle bağımsız olduğu için yakalanır.

## Kapsam

| Katman | Ne sınanır |
|---|---|
| `crc8` | CRC-8/SMBUS (poly 0x07, init 0x00) — paylaşılan saf fonksiyon |
| `messages` | Tüm `std_msgs` tiplerinin `to_bytes()` çıktısı (+ Python'da round-trip) |
| `frames` | Tam wire frame: `HEADER + LEN + DATA + CRC` (Framer) |

Frame vektörleri `(ch_id, seq, payload)` ile ifade edilir. C++ tarafında `seq`,
core'un opak `head` öneki olarak verilir (core SEQ bilmez); Python tarafında
`Framer.build(ch_id, seq, payload)` ile. İkisi de aynı wire byte'larını üretmeli.

## Dosyalar

| Dosya | Rol |
|---|---|
| `generate.py` | Spec tabanlı oracle → `vectors.json` + `vectors.hpp` üretir |
| `vectors.json` | Üretilen vektörler — Python testi okur (üretilmiş, commit'li) |
| `vectors.hpp` | Üretilen C++ header — C++ testi include eder (üretilmiş, commit'li) |
| `cpp/test_conformance.cpp` | minros'u vektörlere karşı sınar (PlatformIO gerekmez) |
| `../lib/minrospy/tests/test_conformance.py` | minrospy'yi vektörlere karşı sınar (pytest) |
| `run.sh` | Üret + iki tarafı da koştur |

> `vectors.json` ve `vectors.hpp` **üretilmiş** dosyalardır. Vektör eklemek için
> `generate.py`'yi düzenleyip yeniden üret — bu dosyaları elle düzenleme.

## Çalıştırma

```bash
# hepsi birden
./conformance/run.sh

# yalnız Python
python3 -m pytest lib/minrospy/tests/test_conformance.py -v

# yalnız C++
python3 conformance/generate.py   # vektörler güncel değilse
g++ -std=c++17 -I lib/minros conformance/cpp/test_conformance.cpp -o /tmp/ct && /tmp/ct
```

## Yeni mesaj/alan eklerken

1. [generate.py](generate.py) içindeki `MESSAGES` (veya `CRC8` / `FRAMES`) listesine
   yeni vektör ekle.
2. `python3 conformance/generate.py` ile `vectors.json` + `vectors.hpp`'yi yenile.
3. `./conformance/run.sh` — iki taraf da geçmeli.

Yeni bir `std_msgs` tipi eklediysen `test_conformance.py` içindeki `CLASSES`
sözlüğüne sınıfı da ekle.
