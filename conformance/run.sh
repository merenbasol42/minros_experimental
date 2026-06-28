#!/usr/bin/env bash
# minros wire-protokolü conformance sürücüsü.
#
# 1) Tarafsız altın vektörleri üretir (spec'ten — generate.py)
# 2) C++ tarafını (minros) vektörlere karşı derleyip çalıştırır
# 3) Python tarafını (minrospy) vektörlere karşı pytest ile çalıştırır
#
# İki implementasyon birbirinden kaymışsa ilgili taraf kırmızı döner.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

echo "== 1/3 vektörleri üret =="
python3 "$HERE/generate.py"

echo "== 2/3 C++ (minros) =="
BIN="$(mktemp -d)/ct"
g++ -std=c++17 -Wall -I "$ROOT/lib/minros" "$HERE/cpp/test_conformance.cpp" -o "$BIN"
"$BIN"

echo "== 3/3 Python (minrospy) =="
python3 -m pytest "$ROOT/lib/minrospy/tests/test_conformance.py" -q

echo "== conformance: iki taraf da uyumlu =="
