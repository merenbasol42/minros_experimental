# minros_experimental

ESP32-S3 üzerinde [minros](https://github.com/merenbasol42/minros) kütüphanesini
denemek için hazırlanmış PlatformIO projesidir. minros ayrı bir repo'da yaşar ve
buraya `lib_deps` ile çekilir; bu repo yalnızca **deneme firmware'i + test
betikleri** (ve şimdilik `minrospy`) barındırır.

## Hedef Donanım

| Parametre | Değer |
|---|---|
| Board | ESP32-S3 Box |
| Framework | Arduino |
| C++ standardı | C++17 |

## Proje Yapısı

```
minros_experimental/
├── src/main.cpp         ← deneme firmware'i (echo + parameters)
├── tools/               ← host tarafı test betikleri (minros_serial.py, …)
├── lib/minrospy/        ← minros'un Python portu (şimdilik burada)
├── test/ conformance/   ← minros header'larına dayalı testler
└── platformio.ini       ← minros lib_deps ile buradan gelir
```

## minros bağımlılığı

minros artık ayrı repo'dan gelir; kaynağı bu proje içinde tutulmaz:

```ini
; platformio.ini
[env]
lib_deps = https://github.com/merenbasol42/minros.git#v0.1.0
```

Sürümü yükseltmek için tag'i değiştir (`#v0.2.0`) ya da `pio pkg update`. minros
`.pio/libdeps/` altına klonlanır — elle kopyalama/silme yoktur.

## Başlangıç

```bash
pio run -t upload
pio device monitor
```

## Notlar

- Bu repo yalnızca deney amaçlıdır; minros'un asıl evi
  [merenbasol42/minros](https://github.com/merenbasol42/minros)'tur.
- Host tarafı denemeler için `tools/minros_serial.py` (echo + `<p>` ile param set).
