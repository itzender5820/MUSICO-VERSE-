#!/data/data/com.termux/files/usr/bin/bash
set -e
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  MUSICO VERSE v8 - Termux Build"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

echo "[1/5] Installing dependencies..."
pkg update -y
pkg install -y cmake clang make ffmpeg ncurses libc++ python termux-tools

echo "[2/5] Installing Python lyrics fetcher..."
pip install syncedlyrics 2>/dev/null || pip3 install syncedlyrics 2>/dev/null || \
    echo "  (syncedlyrics optional — lyrics will still load from .lrc files)"

echo "[3/5] Storage permission..."
termux-setup-storage 2>/dev/null || true
mkdir -p /sdcard/music 2>/dev/null || true

echo "[4/5] Configuring..."
mkdir -p build && cd build
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DANDROID=TRUE \
    -DCURSES_INCLUDE_PATH="$PREFIX/include" \
    -DCURSES_LIBRARY="$PREFIX/lib/libncurses.so"

echo "[5/5] Building..."
make -j$(nproc)

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  BUILD COMPLETE  →  ./build/musico_verse"
echo ""
echo "  Music dir : /sdcard/music/ (recursive)"
echo "  Formats   : flac mp3 m4a opus ogg wav"
echo "  Lyrics    : auto-fetched via syncedlyrics"
echo "  Settings  : press S in player"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
