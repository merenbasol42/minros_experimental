#!/usr/bin/env bash
# minros wire-protokolü conformance sürücüsü.
#
# 1) Tarafsız altın vektörleri üretir (spec'ten — generate.py)
# 2) C++ tarafını (minros) vektörlere karşı derleyip çalıştırır
# 3) Python tarafını (minrospy) vektörlere karşı pytest ile çalıştırır
#
# İki implementasyon birbirinden kaymışsa ilgili taraf kırmızı döner.
#
# minros/minrospy artık lib/minros ve lib/minrospy altında git submodule
# olarak yaşıyor (bkz. ../.gitmodules) — yani buradaki testler her zaman
# local, henüz publish edilmemiş değişiklikleri sınar:
#   • C++ header'ları:  lib/minros
#     Elle geçersiz kılmak için:  MINROS_INC=<minros_kök> conformance/run.sh
#   • Python paketi:    geçici bir venv'e lib/minrospy'den editable kurulur
#     (sistem python'unu / ROS ortamını hiç kirletmez). Çıkışta silinir.
#     Elle geçersiz kılmak için: MINROSPY_SRC=<minrospy_kök> conformance/run.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

VENV=""
cleanup() { [ -n "$VENV" ] && rm -rf "$VENV"; }
trap cleanup EXIT

echo "== 1/3 vektörleri üret =="
python3 "$HERE/generate.py"

# ── minros C++ header kökünü çöz ─────────────────────────────────────────────
MINROS_INC="${MINROS_INC:-$ROOT/lib/minros}"
if [ ! -f "$MINROS_INC/minros/core/wireframe.hpp" ]; then
    echo "hata: $MINROS_INC altında minros bulunamadı." >&2
    echo "      önce 'git submodule update --init' çalıştır (ya da MINROS_INC ile elle göster)." >&2
    exit 1
fi

echo "== 2/3 C++ (minros) — $MINROS_INC =="
BIN="$(mktemp -d)/ct"
g++ -std=c++17 -Wall -I "$MINROS_INC" "$HERE/cpp/test_conformance.cpp" -o "$BIN"
"$BIN"

# ── izole venv → local minrospy (editable) ───────────────────────────────────
MINROSPY_SRC="${MINROSPY_SRC:-$ROOT/lib/minrospy}"
if [ ! -f "$MINROSPY_SRC/pyproject.toml" ]; then
    echo "hata: $MINROSPY_SRC altında minrospy bulunamadı." >&2
    echo "      önce 'git submodule update --init' çalıştır (ya da MINROSPY_SRC ile elle göster)." >&2
    exit 1
fi

echo "== 3/3 Python (minrospy — local: $MINROSPY_SRC) =="
VENV="$(mktemp -d)/venv"
python3 -m venv "$VENV"
# PYTHONPATH boşaltılır: ROS (jazzy) sourced ortamda sistem paketlerinin venv'i
# gölgelemesini engeller — böylece gerçekten local minrospy sınanır.
env -u PYTHONPATH "$VENV/bin/pip" install --quiet --upgrade pip pytest
env -u PYTHONPATH "$VENV/bin/pip" install --quiet -e "$MINROSPY_SRC"
echo "   kurulu: minrospy $("$VENV/bin/python" -c 'import minrospy; print(minrospy.__version__)') (editable, local)"
env -u PYTHONPATH "$VENV/bin/python" -m pytest "$HERE/py/test_conformance.py" -q

echo "== conformance: iki taraf da uyumlu =="
