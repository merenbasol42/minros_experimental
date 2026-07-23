# firmware — minros deneme firmware'i

ESP32-S3 üzerinde [minros](https://github.com/merenbasol42/minros) kütüphanesini
denemek için hazırlanmış PlatformIO projesidir. `minros`, kendi ayrı GitHub
reposunda yaşamaya devam ediyor ama buraya **git submodule** olarak
(`lib/minros`) bağlı — yani publish etmeden, doğrudan burada değiştirip
test edebilirsin.

## Hedef Donanım

| Parametre | Değer |
|---|---|
| Board | ESP32-S3 Box |
| Framework | Arduino |
| C++ standardı | C++17 |

## Proje Yapısı

```
firmware/
├── src/main.cpp   ← deneme firmware'i (echo + parameters)
├── test/          ← Unity/pio test (test_communication, test_raw_node)
├── lib/minros/    ← git submodule → github.com/merenbasol42/minros
└── platformio.ini ← lib/ otomatik taranır, lib_deps gerekmez
```

## Bağımlılık — submodule olarak

`lib/minros`, `minros` reposunun birebir checkout'u. Bu bir kopya değil —
klasörün içi gerçekten o reponun kendisi:

```bash
# taze klon (repo kökünden, submodule'lerle birlikte)
git clone --recurse-submodules git@github.com:merenbasol42/minros_experimental.git

# ya da zaten klonlandıysa (repo kökünden)
git submodule update --init

# minros'un ana branch'inin ucuna ilerlet
git submodule update --remote firmware/lib/minros
```

**Geliştirme akışı**: `firmware/lib/minros` içine gir, normal şekilde
değiştir, orada `git commit` + `git push` at — bu **doğrudan** gerçek
`minros` reposuna gider, ayrı bir yayın/senkron adımı yok. Sonra bu reponun
kökünde `git add firmware/lib/minros && git commit` ile "şu an hangi
commit'e bakıyorum" bilgisini (pointer) güncelle.

## Başlangıç

```bash
cd firmware
pio run -t upload
pio device monitor
```

## Notlar

- Host tarafı denemeler için [../host/](../host/).
- İki implementasyonun (`minros` ↔ `minrospy`) aynı wire protokolüne
  uyduğunu doğrulamak için [../conformance/](../conformance/) —
  `./conformance/run.sh` her zaman buradaki local `lib/minros`'u sınar.
