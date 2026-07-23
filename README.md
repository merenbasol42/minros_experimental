# minros_experimental

ESP32-S3 üzerinde [minros](https://github.com/merenbasol42/minros) kütüphanesini
denemek için hazırlanmış PlatformIO projesidir. minros ve Python portu
`minrospy`, kendi ayrı GitHub repolarında yaşamaya devam ediyor ama buraya
**git submodule** olarak (`lib/minros`, `lib/minrospy`) bağlı — yani ikisini
birlikte, publish etmeden, tek yerden değiştirip test edebilirsin.

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
├── test/ conformance/   ← minros/minrospy'e karşı testler
├── lib/minros/          ← git submodule → github.com/merenbasol42/minros
├── lib/minrospy/        ← git submodule → github.com/merenbasol42/minrospy
└── platformio.ini       ← lib/ otomatik taranır, lib_deps gerekmez
```

## Bağımlılıklar — submodule olarak

**C++ (minros)** ve **Python (minrospy)**, kendi repolarının birebir
checkout'ları olarak `lib/minros` ve `lib/minrospy` altında yaşıyor. Bu bir
kopya değil — o klasörlerin içi gerçekten o repoların kendisi:

```bash
# taze klon (submodule'lerle birlikte)
git clone --recurse-submodules git@github.com:merenbasol42/minros_experimental.git

# ya da zaten klonlandıysa
git submodule update --init

# ana branch'lerinin ucuna ilerlet
git submodule update --remote
```

**Geliştirme akışı**: `lib/minros` veya `lib/minrospy` içine gir, normal
şekilde değiştir, orada `git commit` + `git push` at — bu **doğrudan** gerçek
`minros`/`minrospy` reposuna gider, ayrı bir yayın/senkron adımı yok. Sonra bu
reponun kökünde `git add lib/minros lib/minrospy && git commit` ile
"şu an hangi commit'e bakıyorum" bilgisini (pointer) güncelle.

`tools/requirements.txt` hâlâ pip üzerinden PyPI'daki minrospy'yi kurar (host
test betikleri için); local geliştirme + conformance testi ise her zaman
`lib/minrospy`'yi kullanır (bkz. `conformance/run.sh`).

## Başlangıç

```bash
pio run -t upload
pio device monitor
```

## Notlar

- minros ve minrospy'nin kendi GitHub sayfaları / PyPI yayını değişmedi:
  [merenbasol42/minros](https://github.com/merenbasol42/minros),
  [merenbasol42/minrospy](https://github.com/merenbasol42/minrospy) →
  [pypi.org/project/minrospy](https://pypi.org/project/minrospy/). Bu repo
  ikisini **birlikte geliştirip test etmek** için ortak bir çalışma alanı.
- Host tarafı denemeler için `tools/minros_serial.py` (echo + `<p>` ile param set).
- İki implementasyonun aynı wire protokolüne uyduğunu doğrulamak için
  `./conformance/run.sh` — her zaman `lib/` altındaki local kaynağı sınar.
