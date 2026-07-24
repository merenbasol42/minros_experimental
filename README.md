# minros_experimental

[minros](https://github.com/merenbasol42/minros) (C++, gömülü) ve Python portu
[minrospy](https://github.com/merenbasol42/minrospy)'yi birlikte geliştirip,
publish etmeden önce gerçek donanımla ve birbirlerine karşı test etmek için
hazırlanmış çalışma alanı. Üç bağımsız alt proje halinde bölünmüş:

| Klasör | Ne | Detay |
|---|---|---|
| [firmware/](firmware/) | ESP32-S3 üzerinde çalışan deneme firmware'i (PlatformIO) | [firmware/README.md](firmware/README.md) |
| [host/](host/) | Gerçek karta karşı seri port üzerinden çalışan Python betikleri | [host/README.md](host/README.md) |
| [conformance/](conformance/) | `minros` ↔ `minrospy` wire-protokolü uyumluluk testleri | [conformance/README.md](conformance/README.md) |

## Neden üç ayrı klasör?

`firmware/` ve `host/` birbirinden bağımsız çalıştırılabilir projeler (biri
PlatformIO, biri Python) — her biri kendi bağımlılık zincirine ve kendi
`minros`/`minrospy` submodule'ına sahip:

- `firmware/lib/minros` — submodule, `minros`'un kendisi.
- `host/lib/minrospy` — submodule, `minrospy`'nin kendisi.

`conformance/` ikisini de aynı anda sınadığı için doğası gereği her iki
submodule'a da erişir (bkz. [conformance/README.md](conformance/README.md)).

## Başlangıç

```bash
# taze klon (submodule'lerle birlikte)
git clone --recurse-submodules git@github.com:merenbasol42/minros_experimental.git
# ya da zaten klonlandıysa
git submodule update --init

# firmware
cd firmware && pio run -t upload && pio device monitor

# host (ayrı terminalde, kart bağlıyken)
cd host && pip install -r requirements.txt && minros-serial

# conformance
./conformance/run.sh
```

## Notlar

- `minros` ve `minrospy`'nin kendi GitHub sayfaları / PyPI yayını değişmedi:
  [merenbasol42/minros](https://github.com/merenbasol42/minros),
  [merenbasol42/minrospy](https://github.com/merenbasol42/minrospy) →
  [pypi.org/project/minrospy](https://pypi.org/project/minrospy/). Bu repo
  ikisini **birlikte geliştirip test etmek** için ortak bir çalışma alanı.
- Submodule geliştirme akışı (commit/push, pointer güncelleme) için
  [firmware/README.md](firmware/README.md) ve [host/README.md](host/README.md).
