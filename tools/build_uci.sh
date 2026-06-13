#!/usr/bin/env bash
# Build the portable UCI engine (no Windows GUI dependencies).
# Produces build_uci/chess_uci — a standard UCI engine usable by any chess GUI
# and by the lichess-bot bridge. Works on Linux and on Windows (MinGW g++).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mkdir -p build_uci
OPT="-std=c++17 -O3 -march=native -funroll-loops -pthread -Iinclude"
SRC="src/core/Piece.cpp src/core/Board.cpp src/search/Search.cpp src/search/OpeningBook.cpp src/uci.cpp"
echo "[build] compiling UCI engine..."
g++ $OPT $SRC -o build_uci/chess_uci
echo "[build] done -> build_uci/chess_uci"
