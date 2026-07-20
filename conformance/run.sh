#!/usr/bin/env bash
# minros wire-protokolü conformance sürücüsü.
#
# 1) Tarafsız altın vektörleri üretir (spec'ten — generate.py)
# 2) C++ tarafını (minros) vektörlere karşı derleyip çalıştırır
# 3) Python tarafını (minrospy) vektörlere karşı pytest ile çalıştırır
#
# İki implementasyon birbirinden kaymışsa ilgili taraf kırmızı döner.
#
# Kendi kendine yeter — kurulum gerektirmez. minros/minrospy ayrı repolarda yaşar:
#   • C++ header'ları: .pio/libdeps'ten (pio run sonrası) alınır; yoksa
#     platformio.ini'deki pinli tag'den geçici olarak klonlanır.
#     Elle geçersiz kılmak için:  MINROS_INC=<minros_kök> conformance/run.sh
#   • Python paketi: geçici bir venv kurulur ve PyPI'dan EN SON minrospy + pytest
#     indirilir (sistem python'unu / ROS ortamını hiç kirletmez). Çıkışta silinir.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

TMPCLONE=""
VENV=""
cleanup() { [ -n "$TMPCLONE" ] && rm -rf "$TMPCLONE"; [ -n "$VENV" ] && rm -rf "$VENV"; }
trap cleanup EXIT

echo "== 1/3 vektörleri üret =="
python3 "$HERE/generate.py"

# ── minros C++ header kökünü çöz ─────────────────────────────────────────────
MINROS_INC="${MINROS_INC:-$ROOT/.pio/libdeps/esp32s3box/minros}"
if [ ! -f "$MINROS_INC/minros/core/wireframe.hpp" ]; then
    URL="$(grep -oP 'https://\S+minros\.git' "$ROOT/platformio.ini" | head -1)"
    TAG="$(grep -oP 'minros\.git#\K\S+' "$ROOT/platformio.ini" | head -1)"
    echo "   libdeps'te minros yok; klonlanıyor: $URL @ ${TAG:-default}"
    TMPCLONE="$(mktemp -d)"
    git clone --quiet --depth 1 ${TAG:+--branch "$TAG"} "$URL" "$TMPCLONE/minros"
    MINROS_INC="$TMPCLONE/minros"
fi

echo "== 2/3 C++ (minros) — $MINROS_INC =="
BIN="$(mktemp -d)/ct"
g++ -std=c++17 -Wall -I "$MINROS_INC" "$HERE/cpp/test_conformance.cpp" -o "$BIN"
"$BIN"

# ── izole venv → PyPI'dan en son minrospy + pytest ───────────────────────────
echo "== 3/3 Python (minrospy — PyPI'dan en son) =="
VENV="$(mktemp -d)/venv"
python3 -m venv "$VENV"
# PYTHONPATH boşaltılır: ROS (jazzy) sourced ortamda sistem paketlerinin venv'i
# gölgelemesini engeller — böylece gerçekten pip'ten kurulan minrospy sınanır.
env -u PYTHONPATH "$VENV/bin/pip" install --quiet --upgrade pip minrospy pytest
echo "   kurulu: minrospy $("$VENV/bin/python" -c 'import minrospy; print(minrospy.__version__)')"
env -u PYTHONPATH "$VENV/bin/python" -m pytest "$HERE/py/test_conformance.py" -q

echo "== conformance: iki taraf da uyumlu =="
