#!/bin/bash
#
# build-native.sh - build the native part of the C64-EmuDis Android app
#
# Usage: build-native.sh <source dir> <work dir>
#
# Builds for arm64-v8a and x86_64 with the Android NDK (ANDROID_NDK_HOME or
# ANDROID_NDK_LATEST_HOME):
#
#   SDL2 (shared, libSDL2.so) and SDL2_image (static) from GitHub
#   VICE x64sc with the SDL2 UI as libmain.so (SDL calls SDL_main in it)
#
# and copies into the Android project (<source dir>/android/app/src/main):
#
#   jniLibs/<abi>/libSDL2.so, libmain.so, libc++_shared.so
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

# arm64-v8a: phones and tablets; x86_64: Chromebooks and the Android
# emulator (used by the CI smoke test)
ABIS="${EMUDIS_ANDROID_ABIS:-arm64-v8a x86_64}"
API=24
JOBS=$(nproc)

NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_LATEST_HOME:-}}"
if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
    echo "ERROR: Android NDK not found (set ANDROID_NDK_HOME)"
    exit 1
fi
echo "NDK: $NDK"
TC="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
APP="$SRC/android/app/src/main"
PAGE="-Wl,-z,max-page-size=16384"      # 16 KB pages (Android 15+)
STRIP="$TC/bin/llvm-strip"

[ -d "$WORK/SDL2" ] || git clone -q --depth 1 -b $SDL_TAG https://github.com/libsdl-org/SDL.git "$WORK/SDL2"
[ -d "$WORK/SDL2_image" ] || git clone -q --depth 1 -b $SDLIMAGE_TAG https://github.com/libsdl-org/SDL_image.git "$WORK/SDL2_image"

rm -rf "$APP/jniLibs"

build_abi() {
    local ABI=$1 TRIPLE
    case $ABI in
        arm64-v8a) TRIPLE=aarch64-linux-android ;;
        x86_64)    TRIPLE=x86_64-linux-android ;;
        *) echo "unknown ABI $ABI"; exit 1 ;;
    esac
    local PFX="$WORK/prefix-$ABI"
    local CMAKE_ANDROID=(-G Ninja
        -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake"
        -DANDROID_ABI=$ABI -DANDROID_PLATFORM=android-$API
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PFX"
        -DCMAKE_PREFIX_PATH="$PFX" -DCMAKE_FIND_ROOT_PATH="$PFX"
        -DCMAKE_SHARED_LINKER_FLAGS="$PAGE")

    echo "=== $ABI"

    # SDL2 (shared)
    if [ ! -f "$PFX/lib/libSDL2.so" ]; then
        rm -rf "$WORK/build-sdl-$ABI"
        cmake -S "$WORK/SDL2" -B "$WORK/build-sdl-$ABI" "${CMAKE_ANDROID[@]}" \
              -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF
        cmake --build "$WORK/build-sdl-$ABI" -j $JOBS
        cmake --install "$WORK/build-sdl-$ABI"
    fi

    # SDL2_image (static, stb_image only)
    if [ ! -f "$PFX/lib/libSDL2_image.a" ]; then
        rm -rf "$WORK/build-sdlimage-$ABI"
        cmake -S "$WORK/SDL2_image" -B "$WORK/build-sdlimage-$ABI" "${CMAKE_ANDROID[@]}" \
              -DBUILD_SHARED_LIBS=OFF -DSDL2IMAGE_SAMPLES=OFF -DSDL2IMAGE_TESTS=OFF \
              -DSDL2IMAGE_AVIF=OFF -DSDL2IMAGE_JXL=OFF -DSDL2IMAGE_TIF=OFF -DSDL2IMAGE_WEBP=OFF \
              -DSDL2IMAGE_VENDORED=OFF -DSDL2IMAGE_BACKEND_STB=ON -DSDL2IMAGE_DEPS_SHARED=OFF
        cmake --build "$WORK/build-sdlimage-$ABI" -j $JOBS
        cmake --install "$WORK/build-sdlimage-$ABI"
    fi

    # VICE (x64sc, SDL2 UI)
    local VB="$WORK/vice-build-$ABI"
    (
    export CC="$TC/bin/${TRIPLE}${API}-clang"
    export CXX="$TC/bin/${TRIPLE}${API}-clang++"
    export AR="$TC/bin/llvm-ar"
    export RANLIB="$TC/bin/llvm-ranlib"
    export STRIP NM="$TC/bin/llvm-nm"
    export PKG_CONFIG_LIBDIR="$PFX/lib/pkgconfig:$PFX/share/pkgconfig"
    export PKG_CONFIG_PATH=""
    export CFLAGS="-O2 -g -fPIC"
    export CXXFLAGS="-O2 -g -fPIC"
    export LDFLAGS="$PAGE"
    export LIBS="-llog -landroid"

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
    )

    # copy into the Android project
    mkdir -p "$APP/jniLibs/$ABI" "$WORK/symbols/$ABI"
    cp "$VB/src/x64sc" "$WORK/symbols/$ABI/libmain.so"      # with debug info
    cp "$VB/src/x64sc" "$APP/jniLibs/$ABI/libmain.so"
    cp "$PFX/lib/libSDL2.so" "$APP/jniLibs/$ABI/libSDL2.so"
    "$STRIP" --strip-debug "$APP/jniLibs/$ABI/libmain.so" "$APP/jniLibs/$ABI/libSDL2.so"

    # libc++_shared.so: VICE (reSID is C++) and maybe SDL need it
    for lib in libmain.so libSDL2.so; do
        if "$TC/bin/llvm-readelf" -d "$APP/jniLibs/$ABI/$lib" | grep -q "libc++_shared"; then
            cp "$TC/sysroot/usr/lib/$TRIPLE/libc++_shared.so" "$APP/jniLibs/$ABI/"
        fi
    done
    ls -la "$APP/jniLibs/$ABI"
}

for abi in $ABIS; do
    build_abi $abi
done

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

echo "assets: $(wc -l < "$APP/assets/vice/files.txt") files"
