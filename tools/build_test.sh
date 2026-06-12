#!/usr/bin/env bash
# Headless build of the portable engine core + test harness (Linux/g++).
# The Windows GUI (ui/, main_gui.cpp) is intentionally excluded — only the
# portable engine (core/, search/) is compiled, plus the test harness.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mkdir -p build_test
OPT="-std=c++17 -O3 -march=native -funroll-loops -pthread -Iinclude"
SRC="src/core/Piece.cpp src/core/Board.cpp src/search/Search.cpp src/search/OpeningBook.cpp tools/engine_test.cpp"
echo "[build] compiling headless engine + harness..."
g++ $OPT $SRC -o build_test/engine_test
echo "[build] done -> build_test/engine_test"
