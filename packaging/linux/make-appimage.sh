#!/bin/bash
#
# make-appimage.sh - build the Linux AppImage of C64-EmuDis
#
# Usage: make-appimage.sh <source dir> <portable dist dir> <output dir>
#
#   <portable dist dir> is the folder created by make-dist.sh
#   (C64-EmuDis-<version>-linux-<arch>/).
#
# The AppImage contains the emulator, its data, GTK 3 (via
# linuxdeploy-plugin-gtk), GLEW and a static ffmpeg for video recording.
# OpenGL itself (libGL) always comes from the host system.
#
# Tools are downloaded from GitHub:
#   linuxdeploy, linuxdeploy-plugin-gtk, linuxdeploy-plugin-appimage
#   BtbN/FFmpeg-Builds (static GPL ffmpeg)
#
# This file is part of C64-EmuDis (GPL-2.0-or-later).

set -o errexit
set -o nounset
set -o pipefail

SRC="$(cd "$1" && pwd)"
DIST="$(cd "$2" && pwd)"
OUT="$3"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"

VERSION=$(sed -n 's/^#define EMUDIS_VERSION *"\(.*\)"/\1/p' "$SRC/src/emudis_version.h")
ARCH=$(uname -m)
NAME="C64-EmuDis-$VERSION-linux-$ARCH.AppImage"

WORK="$OUT/appimage-work"
APPDIR="$WORK/AppDir"
TOOLS="$WORK/tools"
rm -rf "$WORK"
mkdir -p "$APPDIR/usr/bin" "$TOOLS"

# the AppImage tools are AppImages themselves; no FUSE in containers/CI
export APPIMAGE_EXTRACT_AND_RUN=1

fetch() {
    # fetch <url> <file>
    echo "Downloading $1"
    curl -fsSL --retry 3 -o "$2" "$1"
}

fetch https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage \
      "$TOOLS/linuxdeploy"
fetch https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/continuous/linuxdeploy-plugin-appimage-$ARCH.AppImage \
      "$TOOLS/linuxdeploy-plugin-appimage"
fetch https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh \
      "$TOOLS/linuxdeploy-plugin-gtk.sh"
chmod +x "$TOOLS"/*
export PATH="$TOOLS:$PATH"

# --- emulator + data ------------------------------------------------------
# VICE finds its data next to the executable (boot path), so everything goes
# into usr/bin.
(cd "$DIST" && tar --exclude='*.desktop' --exclude='install-desktop-entry.sh' \
                   --exclude='c64-emudis.png' -cf - .) | (cd "$APPDIR/usr/bin" && tar xf -)

# --- static ffmpeg for the video recorder --------------------------------
if [ "$ARCH" = "x86_64" ]; then
    fetch https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-linux64-gpl.tar.xz \
          "$WORK/ffmpeg.tar.xz"
    mkdir -p "$WORK/ffmpeg"
    tar -xJf "$WORK/ffmpeg.tar.xz" -C "$WORK/ffmpeg" --strip-components=1
    cp "$WORK/ffmpeg/bin/ffmpeg" "$APPDIR/usr/bin/ffmpeg"
    chmod +x "$APPDIR/usr/bin/ffmpeg"
    mkdir -p "$APPDIR/usr/share/doc/ffmpeg"
    cp "$WORK/ffmpeg/LICENSE.txt" "$APPDIR/usr/share/doc/ffmpeg/" 2>/dev/null || true
fi

# --- desktop entry + icon --------------------------------------------------
mkdir -p "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps"
cp "$SRC/packaging/linux/c64-emudis.desktop" "$APPDIR/usr/share/applications/"
cp "$SRC/data/common/vice-x64_256.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/c64-emudis.png"

# --- bundle libraries (GTK 3, GLEW, ...) and build the AppImage ----------
export DEPLOY_GTK_VERSION=3
export LDAI_OUTPUT="$OUT/$NAME"
export OUTPUT="$OUT/$NAME"
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"

cd "$WORK"
linuxdeploy --appdir "$APPDIR" \
            --executable "$APPDIR/usr/bin/C64-EmuDis" \
            --desktop-file "$APPDIR/usr/share/applications/c64-emudis.desktop" \
            --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/c64-emudis.png" \
            --plugin gtk \
            --output appimage

# the appimage plugin may ignore OUTPUT on some versions: pick up the result
if [ ! -f "$OUT/$NAME" ]; then
    built=$(ls -1t "$WORK"/*.AppImage 2>/dev/null | head -n 1 || true)
    [ -n "$built" ] && mv "$built" "$OUT/$NAME"
fi
chmod +x "$OUT/$NAME"
rm -rf "$WORK"
echo "Done: $OUT/$NAME"
