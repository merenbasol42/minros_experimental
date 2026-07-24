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
├── pyproject.toml           ← minros-tools paketi + console_scripts tanımı
├── requirements.txt
├── minros_tools/             ← paket
│   ├── common.py             ← ortak yardımcılar (pyserial transport, spin döngüsü, …)
│   ├── serial_monitor.py     ← seri monitör: Vector3 gönder/al, terminalde göster
│   ├── latency.py            ← gecikme ölçümü
│   ├── latency_rel.py        ← reliable overlay üzerinden gecikme ölçümü
│   ├── throughput.py         ← throughput ölçümü
│   ├── ack_test.py           ← reliability/ACK testi
│   └── parser_resilience.py  ← parser dayanıklılığı testi (gerçek kart üzerinden)
├── test/                     ← donanımsız birim testler (bkz. Testler bölümü)
│   ├── conftest.py            ← ortak fixture'lar (Parser/Framer/Collector)
│   └── test_parser_resilience.py  ← Parser'ın bozuk/eksik akıştan toparlanma testleri
└── lib/minrospy/             ← git submodule → github.com/merenbasol42/minrospy
```

## Kurulum

```bash
cd host
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt   # minrospy'yi lib/minrospy'den, minros-tools'u host/'un kendisinden editable kurar
```

**Geliştirme akışı**: `lib/minrospy` içine gir, normal şekilde değiştir,
orada `git commit` + `git push` at — bu **doğrudan** gerçek `minrospy`
reposuna gider. Sonra bu reponun kökünde `git add host/lib/minrospy &&
git commit` ile pointer'ı güncelle.

## Çalıştırma

Kart `firmware/` ile flashlanmış ve seri porta bağlıyken (kurulumdan sonra
komutlar PATH'te hazır):

```bash
minros-serial [PORT] [BAUD]       # seri monitör, varsayılan: /dev/ttyACM0 115200
minros-latency [PORT] [BAUD]      # gecikme ölçümü
minros-latency-rel [PORT] [BAUD]  # reliable overlay üzerinden gecikme ölçümü
minros-throughput [PORT] [BAUD] [MESAJ_SAYISI] [HEDEF_RATE_MSG_S]
minros-ack-test [PORT] [BAUD]     # reliability/ACK testi
minros-parser-resilience [PORT] [BAUD]  # parser dayanıklılığı testi (gerçek kart üzerinden)
```

## Testler

Gerçek karta ihtiyaç duymayan, minrospy'nin core protokol katmanını (Parser/
Framer) bellek içi byte akışlarıyla sınayan testler `test/` altında:

```bash
pip install -e ".[test]"   # pytest'i kur
python -m pytest test/
```

**Not:** ROS ortamı `source`lanmışsa (`PYTHONPATH`'te `/opt/ros/...` varsa),
ROS'un `launch_testing` pytest eklentisi otomatik yüklenmeye çalışıp
`ModuleNotFoundError: yaml` ile patlayabilir. Bu durumda:
`PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -m pytest test/`.

## Notlar

- Firmware tarafı için [../firmware/](../firmware/).
- İki implementasyonun aynı wire protokolüne uyduğunu doğrulamak için
  [../conformance/](../conformance/).
