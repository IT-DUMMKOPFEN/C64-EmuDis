#!/bin/bash
#
# make-dist.sh - build the Windows distribution of C64-EmuDis (MSYS2 UCRT64)
#
# Based on VICE's src/arch/gtk3/make-bindist_win32.sh (GPL-2.0-or-later).
#
# Usage (from the build directory, inside an MSYS2 UCRT64 shell):
#   make-dist.sh <top_srcdir> <output dir>
#
# Result: <output dir>/C64-EmuDis-<version>-windows-x64.zip
#
#   C64-EmuDis-0.1a-windows-x64/
#     bin/C64-EmuDis.exe, c1541.exe, ffmpeg.exe, *.dll
#     C64/ DRIVES/ PRINTER/ GLSL/ common/ hotkeys/   (data, found via bin/..)
#     lib/ share/                                   (GTK runtime files)
#
# This file is part of C64-EmuDis (GPL-2.0-or-later).

set -o errexit
set -o nounset

SRC="$(cd "$1" && pwd)"
OUT="$2"
BUILD="$(pwd)"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"

VERSION=$(sed -n 's/^#define EMUDIS_VERSION *"\(.*\)"/\1/p' "$SRC/src/emudis_version.h")
NAME="C64-EmuDis-$VERSION-windows-x64"
DIST="$OUT/$NAME"

echo "Creating $DIST"
rm -rf "$DIST"
mkdir -p "$DIST/bin"

copy_tree() {
    mkdir -p "$2"
    (cd "$1" && tar --exclude 'Makefile*' -cf - .) | (cd "$2" && tar xf -)
}

# copy the DLLs an executable needs (only those from the MSYS2 prefix)
copy_dlls() {
    ntldd -R "$1" | gawk '/\\bin\\/{print $3;}' | cygpath -f - | while read -r dll; do
        # skip DLLs already in the distribution (ntldd also resolves those)
        if [ -f "$dll" ] && [ ! -f "$DIST/bin/$(basename "$dll")" ]; then
            cp "$dll" "$DIST/bin/"
        fi
    done
}

# executables
cp "$BUILD/src/x64sc.exe" "$DIST/bin/C64-EmuDis.exe"
cp "$BUILD/src/c1541.exe" "$DIST/bin/c1541.exe"
strip "$DIST/bin/C64-EmuDis.exe" "$DIST/bin/c1541.exe"
copy_dlls "$DIST/bin/C64-EmuDis.exe"
copy_dlls "$DIST/bin/c1541.exe"

# ffmpeg for the video recorder
if [ -x "$MINGW_PREFIX/bin/ffmpeg.exe" ]; then
    cp "$MINGW_PREFIX/bin/ffmpeg.exe" "$DIST/bin/"
    copy_dlls "$DIST/bin/ffmpeg.exe"
else
    echo "WARNING: ffmpeg.exe not found, video recording will need a separate ffmpeg"
fi

# GTK runtime: pixbuf loaders, icons, schemas, spawn helpers
cd "$MINGW_PREFIX"
cp bin/gspawn-win64-helper*.exe "$DIST/bin/" 2>/dev/null || true
for l in librsvg-2-2 libxml2-2 liblzma-5; do
    if [ -f "bin/$l.dll" ] && [ ! -f "$DIST/bin/$l.dll" ]; then cp "bin/$l.dll" "$DIST/bin/"; fi
done
cp --parents lib/gdk-pixbuf-2.0/2.10.0/loaders/*pixbufloader*{png,svg,xpm}*.dll "$DIST" 2>/dev/null || \
    cp -r --parents lib/gdk-pixbuf-2.0 "$DIST"
for l in "$DIST"/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.dll; do
    copy_dlls "$l"
done
cp --parents -a share/icons/Adwaita/index.theme "$DIST"
cp --parents -a share/icons/Adwaita/scalable "$DIST" 2>/dev/null || true
cp --parents -a share/icons/Adwaita/symbolic "$DIST" 2>/dev/null || true
cp --parents -a share/icons/hicolor/index.theme "$DIST"
cp --parents share/glib-2.0/schemas/gschemas.compiled "$DIST"
cd "$DIST"
GDK_PIXBUF_MODULEDIR=lib/gdk-pixbuf-2.0/2.10.0/loaders gdk-pixbuf-query-loaders \
    > lib/gdk-pixbuf-2.0/2.10.0/loaders.cache
cd "$BUILD"

# emulator data (VICE on Windows looks in bin/..)
for d in C64 DRIVES PRINTER GLSL hotkeys; do
    copy_tree "$SRC/data/$d" "$DIST/$d"
done
rm -f "$DIST"/C64/sdl_*.v?m
mkdir -p "$DIST/common"
cp "$BUILD/data/common/vice.gresource" "$DIST/common/"
cp "$SRC"/data/common/*.ttf "$SRC/data/common/PetMe-FreeLicense.txt" "$DIST/common/"

# documents
mkdir -p "$DIST/doc"
cp "$SRC/COPYING" "$DIST/LICENSE.txt"
cp "$SRC/README.md" "$DIST/README.md"
cp "$SRC/CREDITS.md" "$DIST/doc/CREDITS.md"
cp "$SRC/README" "$DIST/doc/VICE-README.txt"
cat > "$DIST/Start C64-EmuDis.bat" <<'EOF'
@echo off
start "" "%~dp0bin\C64-EmuDis.exe" %*
EOF
cat > "$DIST/Start C64-EmuDis (software renderer).bat" <<'EOF'
@echo off
rem For virtual machines and PCs where the emulator window stays white
start "" "%~dp0bin\C64-EmuDis.exe" -softrender %*
EOF

(cd "$OUT" && rm -f "$NAME.zip" && zip -r -9 -q "$NAME.zip" "$NAME")
echo "Done: $OUT/$NAME.zip"
