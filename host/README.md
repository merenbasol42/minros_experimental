# host — gerçek karta karşı minrospy denemeleri

Gerçek ESP32-S3 karta [firmware](../firmware/) atıldıktan sonra, o kartla
seri port üzerinden gerçek haberleşme kuran Python betikleri. Hepsi
[minrospy](https://github.com/merenbasol42/minrospy) kullanır.

`minrospy`, kendi ayrı GitHub reposunda yaşamaya devam ediyor ama buraya
**git submodule** olarak (`lib/minrospy`) bağlı ve **editable** kurulur —
yani PyPI'daki yayınlanmış sürüm değil, buradaki local/henüz-publish-
edilmemiş değişiklikler test edilir.

## Proje Yapısı

```
host/
├── common.py              ← ortak yardımcılar (pyserial transport, spin döngüsü, …)
├── minros_serial.py       ← seri monitör: Vector3 gönder/al, terminalde göster
├── minros_latency.py      ← gecikme ölçümü
├── minros_latency_rel.py  ← reliable overlay üzerinden gecikme ölçümü
├── minros_throughput.py   ← throughput ölçümü
├── minros_ack_test.py     ← reliability/ACK testi
├── requirements.txt
└── lib/minrospy/          ← git submodule → github.com/merenbasol42/minrospy
```

## Kurulum

```bash
cd host
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt   # minrospy'yi lib/minrospy'den editable kurar
```

**Geliştirme akışı**: `lib/minrospy` içine gir, normal şekilde değiştir,
orada `git commit` + `git push` at — bu **doğrudan** gerçek `minrospy`
reposuna gider. Sonra bu reponun kökünde `git add host/lib/minrospy &&
git commit` ile pointer'ı güncelle.

## Çalıştırma

Kart `firmware/` ile flashlanmış ve seri porta bağlıyken:

```bash
python3 minros_serial.py [PORT] [BAUD]   # varsayılan: /dev/ttyACM0 115200
```

## Notlar

- Firmware tarafı için [../firmware/](../firmware/).
- İki implementasyonun aynı wire protokolüne uyduğunu doğrulamak için
  [../conformance/](../conformance/).
