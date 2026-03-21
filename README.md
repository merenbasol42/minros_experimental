# minros_experimental

ESP32-S3 üzerinde [minros](lib/minros/README.md) kütüphanesinin denenmesi için hazırlanmış PlatformIO projesidir.

## Hedef Donanım

| Parametre | Değer |
|---|---|
| Board | ESP32-S3 Box |
| Framework | Arduino |
| C++ standardı | C++17 |

## Proje Yapısı

```
minros_experimental/
├── lib/minros/          ← minros kütüphanesi (asıl kaynak)
│   ├── minros/          ← header'lar
│   └── README.md        ← minros dokümantasyonu
├── src/main.cpp         ← test kodu
└── platformio.ini
```

## Başlangıç

```bash
pio run -t upload
pio device monitor
```

## Notlar

- Bu repo yalnızca deney amaçlıdır; minros'un kararlı sürümü `lib/minros/` altındadır.
- minros hakkında ayrıntılı bilgi için [lib/minros/README.md](lib/minros/README.md) dosyasına bakınız.
