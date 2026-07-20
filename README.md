# minros_experimental

ESP32-S3 üzerinde [minros](https://github.com/merenbasol42/minros) kütüphanesini
denemek için hazırlanmış PlatformIO projesidir. minros ayrı bir repo'da yaşar ve
buraya `lib_deps` ile çekilir; Python portu `minrospy` ise PyPI'dan gelir. Bu repo
yalnızca **deneme firmware'i + host test betikleri** barındırır.

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
│   └── requirements.txt ← minrospy + pyserial
├── test/ conformance/   ← minros header'larına dayalı testler
└── platformio.ini       ← minros lib_deps ile buradan gelir
```

## Bağımlılıklar

**C++ (minros)** — ayrı repo'dan, `lib_deps` ile; kaynağı bu projede tutulmaz:

```ini
; platformio.ini
[env]
lib_deps = https://github.com/merenbasol42/minros.git#v0.1.0
```

Sürümü yükseltmek için tag'i değiştir (`#v0.2.0`) ya da `pio pkg update`. minros
`.pio/libdeps/` altına klonlanır — elle kopyalama/silme yoktur.

**Python (minrospy)** — PyPI'dan; host test betikleri için:

```bash
pip install -r tools/requirements.txt   # minrospy + pyserial
```

## Başlangıç

```bash
pio run -t upload
pio device monitor
```

## Notlar

- Bu repo yalnızca deney amaçlıdır. minros'un asıl evi
  [merenbasol42/minros](https://github.com/merenbasol42/minros); Python portu
  [merenbasol42/minrospy](https://github.com/merenbasol42/minrospy) →
  [pypi.org/project/minrospy](https://pypi.org/project/minrospy/).
- Host tarafı denemeler için `tools/minros_serial.py` (echo + `<p>` ile param set).
