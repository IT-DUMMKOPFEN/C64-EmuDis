#!/bin/bash
#
# make-dist.sh - build the portable Linux distribution of C64-EmuDis
#
# Usage: make-dist.sh <source dir> <build dir> <output dir>
#
# Produces <output dir>/C64-EmuDis-<version>-linux-<arch>/ and a .tar.gz.
# The program runs from the extracted folder; it needs GTK3, GLEW and
# (for video recording) ffmpeg from the distribution.
#
# This file is part of C64-EmuDis (GPL-2.0-or-later).

set -o errexit
set -o nounset

SRC="$(cd "$1" && pwd)"
BUILD="$(cd "$2" && pwd)"
OUT="$3"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"

VERSION=$(sed -n 's/^#define EMUDIS_VERSION *"\(.*\)"/\1/p' "$SRC/src/emudis_version.h")
ARCH=$(uname -m)
NAME="C64-EmuDis-$VERSION-linux-$ARCH"
DIST="$OUT/$NAME"

echo "Creating $DIST"
rm -rf "$DIST"
mkdir -p "$DIST"

copy_tree() {
    mkdir -p "$2"
    (cd "$1" && tar --exclude 'Makefile*' -cf - .) | (cd "$2" && tar xf -)
}

# binary
cp "$BUILD/src/x64sc" "$DIST/C64-EmuDis"
strip "$DIST/C64-EmuDis" || true

# data (ROMs, keymaps, palettes, fonts, shaders, hotkeys)
for d in C64 DRIVES PRINTER GLSL hotkeys; do
    copy_tree "$SRC/data/$d" "$DIST/$d"
done
rm -f "$DIST"/C64/sdl_*.v?m
mkdir -p "$DIST/common"
cp "$BUILD/data/common/vice.gresource" "$DIST/common/"
cp "$SRC"/data/common/*.ttf "$SRC/data/common/PetMe-FreeLicense.txt" "$DIST/common/"
cp "$SRC"/data/common/vice-x64_*.png "$DIST/common/" 2>/dev/null || true

# docs + license
mkdir -p "$DIST/doc"
cp "$SRC/COPYING" "$DIST/LICENSE.txt"
cp "$SRC/README.md" "$DIST/README.md"
cp "$SRC/CREDITS.md" "$DIST/doc/CREDITS.md"
cp "$SRC/README" "$DIST/doc/VICE-README.txt"

# desktop integration helper
cp "$SRC/packaging/linux/c64-emudis.desktop" "$DIST/"
cp "$SRC/packaging/linux/install-desktop-entry.sh" "$DIST/"
chmod +x "$DIST/install-desktop-entry.sh"
cp "$SRC/data/common/vice-x64_256.png" "$DIST/c64-emudis.png"

(cd "$OUT" && tar czf "$NAME.tar.gz" "$NAME")
echo "Done: $OUT/$NAME.tar.gz"
