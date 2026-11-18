#!/bin/bash
#
# build-native.sh - build the native part of the C64-EmuDis Android app
#
# Usage: build-native.sh <source dir> <work dir>
#
# Builds for arm64-v8a with the Android NDK (ANDROID_NDK_HOME or
# ANDROID_NDK_LATEST_HOME):
#
#   SDL2 (shared, libSDL2.so) and SDL2_image (static) from GitHub
#   VICE x64sc with the SDL2 UI as libmain.so (SDL calls SDL_main in it)
#
# and copies into the Android project (<source dir>/android/app/src/main):
#
#   jniLibs/arm64-v8a/libSDL2.so, libmain.so
#   java/org/libsdl/app/*.java   (the SDL Java glue of the same SDL version)
#   assets/vice/                 (ROMs, keymaps, palettes, ...)
#
# This file is part of C64-EmuDis (GPL-2.0-or-later).

set -o errexit
set -o nounset
set -o pipefail

SRC="$(cd "$1" && pwd)"
mkdir -p "$2"
WORK="$(cd "$2" && pwd)"

SDL_TAG=release-2.32.10
SDLIMAGE_TAG=release-2.8.12

ABI=arm64-v8a
TRIPLE=aarch64-linux-android
API=24
JOBS=$(nproc)

NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_LATEST_HOME:-}}"
if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
    echo "ERROR: Android NDK not found (set ANDROID_NDK_HOME)"
    exit 1
fi
echo "NDK: $NDK"
TC="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
PFX="$WORK/prefix"
APP="$SRC/android/app/src/main"
PAGE="-Wl,-z,max-page-size=16384"      # 16 KB pages (Android 15+)

CMAKE_ANDROID=(-G Ninja
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake"
    -DANDROID_ABI=$ABI -DANDROID_PLATFORM=android-$API
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PFX"
    -DCMAKE_PREFIX_PATH="$PFX" -DCMAKE_FIND_ROOT_PATH="$PFX"
    -DCMAKE_SHARED_LINKER_FLAGS="$PAGE")

# --- SDL2 ------------------------------------------------------------------
if [ ! -f "$PFX/lib/libSDL2.so" ]; then
    rm -rf "$WORK/SDL2"
    git clone -q --depth 1 -b $SDL_TAG https://github.com/libsdl-org/SDL.git "$WORK/SDL2"
    cmake -S "$WORK/SDL2" -B "$WORK/SDL2/build" "${CMAKE_ANDROID[@]}" \
          -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF
    cmake --build "$WORK/SDL2/build" -j $JOBS
    cmake --install "$WORK/SDL2/build"
fi

# --- SDL2_image (static, stb_image only) -----------------------------------
if [ ! -f "$PFX/lib/libSDL2_image.a" ]; then
    rm -rf "$WORK/SDL2_image"
    git clone -q --depth 1 -b $SDLIMAGE_TAG https://github.com/libsdl-org/SDL_image.git "$WORK/SDL2_image"
    cmake -S "$WORK/SDL2_image" -B "$WORK/SDL2_image/build" "${CMAKE_ANDROID[@]}" \
          -DBUILD_SHARED_LIBS=OFF -DSDL2IMAGE_SAMPLES=OFF -DSDL2IMAGE_TESTS=OFF \
          -DSDL2IMAGE_AVIF=OFF -DSDL2IMAGE_JXL=OFF -DSDL2IMAGE_TIF=OFF -DSDL2IMAGE_WEBP=OFF \
          -DSDL2IMAGE_VENDORED=OFF -DSDL2IMAGE_BACKEND_STB=ON -DSDL2IMAGE_DEPS_SHARED=OFF
    cmake --build "$WORK/SDL2_image/build" -j $JOBS
    cmake --install "$WORK/SDL2_image/build"
fi

# --- VICE (x64sc, SDL2 UI) -------------------------------------------------
export CC="$TC/bin/${TRIPLE}${API}-clang"
export CXX="$TC/bin/${TRIPLE}${API}-clang++"
export AR="$TC/bin/llvm-ar"
export RANLIB="$TC/bin/llvm-ranlib"
export STRIP="$TC/bin/llvm-strip"
export NM="$TC/bin/llvm-nm"
export PKG_CONFIG_LIBDIR="$PFX/lib/pkgconfig:$PFX/share/pkgconfig"
export PKG_CONFIG_PATH=""
export CFLAGS="-O2 -fPIC"
export CXXFLAGS="-O2 -fPIC"
export LDFLAGS="$PAGE"
export LIBS="-llog -landroid"

VB="$WORK/vice-build"
if [ ! -f "$VB/config.status" ]; then
    rm -rf "$VB"
    mkdir -p "$VB"
    (cd "$VB" && "$SRC/configure" \
        --host=$TRIPLE --build=x86_64-pc-linux-gnu \
        --prefix=/vice \
        --enable-sdl2ui --with-sdlsound \
        --disable-arch --disable-no-pic --disable-openmp \
        --disable-html-docs --disable-pdf-docs \
        --disable-realdevice --disable-hardsid --disable-parsid --disable-catweasel \
        --disable-rs232 --disable-ipv6 \
        --without-alsa --without-pulse --without-oss --without-sun \
        --without-libcurl --without-png --without-portaudio \
        || { tail -n 60 config.log; exit 1; })
fi

make -C "$VB/src/lib" -j $JOBS
# link the emulator as a shared library: SDL's Java side loads libmain.so
# and calls SDL_main (SDL_main.h renames main() on Android)
make -C "$VB/src" -j $JOBS x64sc LDFLAGS="-shared $PAGE -Wl,--no-undefined"

# --- copy into the Android project -----------------------------------------
mkdir -p "$APP/jniLibs/$ABI"
cp "$VB/src/x64sc" "$APP/jniLibs/$ABI/libmain.so"
cp "$PFX/lib/libSDL2.so" "$APP/jniLibs/$ABI/libSDL2.so"
"$STRIP" --strip-unneeded "$APP/jniLibs/$ABI/libmain.so" "$APP/jniLibs/$ABI/libSDL2.so"

# libc++_shared.so if SDL was linked against it
if "$TC/bin/llvm-readelf" -d "$APP/jniLibs/$ABI/libSDL2.so" | grep -q "libc++_shared"; then
    cp "$TC/sysroot/usr/lib/$TRIPLE/libc++_shared.so" "$APP/jniLibs/$ABI/"
fi

rm -rf "$APP/java/org/libsdl"
mkdir -p "$APP/java/org/libsdl"
cp -r "$WORK/SDL2/android-project/app/src/main/java/org/libsdl/app" "$APP/java/org/libsdl/"

# VICE data: only what the C64 needs
rm -rf "$APP/assets/vice"
mkdir -p "$APP/assets/vice"
for d in C64 DRIVES PRINTER common; do
    mkdir -p "$APP/assets/vice/$d"
    (cd "$SRC/data/$d" && find . -type f ! -name 'Makefile*' ! -name '*.png' ! -name '*.svg' \
        ! -name '*.ico' ! -name '*.icns' ! -name '*.ttf' ! -name 'gtk3_*' ! -name '*.desktop' -print0 \
        | xargs -0 -I{} cp --parents {} "$APP/assets/vice/$d/")
done
# hotkey files
mkdir -p "$APP/assets/vice/hotkeys"
cp "$SRC"/data/hotkeys/*.vhk "$APP/assets/vice/hotkeys/" 2>/dev/null || true
# a list of all files, the app unpacks them on the first start
(cd "$APP/assets" && find vice -type f | sort > vice/files.txt)

ls -la "$APP/jniLibs/$ABI"
echo "assets: $(wc -l < "$APP/assets/vice/files.txt") files"
