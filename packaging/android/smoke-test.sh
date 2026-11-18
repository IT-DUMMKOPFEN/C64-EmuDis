#!/bin/bash
#
# smoke-test.sh - start the C64-EmuDis APK in a running Android emulator
#
# Usage: smoke-test.sh <apk>
#
# Installs the APK, starts it, and takes screenshots (portrait, keyboard
# overlay, landscape). Screenshots go to smoke/ and, base64 encoded between
# BEGIN/END lines, into the log. The log of the app goes to smoke/logcat.txt.
#
# This file is part of C64-EmuDis (GPL-2.0-or-later).

set -o nounset

APK="$1"
PKG=io.github.itdummkopfen.c64emudis
mkdir -p smoke

shot() {
    # shot <name>: screenshot, scaled down to keep the log small
    adb exec-out screencap -p > "smoke/$1.png"
    python3 - "smoke/$1.png" <<'PY' || true
import sys
try:
    from PIL import Image
    im = Image.open(sys.argv[1]); im.thumbnail((540, 540)); im.save(sys.argv[1], optimize=True)
except Exception as e:
    print("no resize:", e)
PY
    echo "=== SCREENSHOT $1 BEGIN"
    base64 -w 0 "smoke/$1.png"
    echo
    echo "=== SCREENSHOT $1 END"
}

size() {
    adb shell wm size | sed -n 's/.*: *\([0-9]*\)x\([0-9]*\).*/\1 \2/p' | tail -n 1
}

pip3 install --quiet pillow 2>/dev/null || true

adb install -r "$APK" || exit 1
adb logcat -c
adb shell settings put system accelerometer_rotation 0
adb shell settings put system user_rotation 0
adb shell am start -n $PKG/.EmuActivity
sleep 25
shot portrait

# KEYS button: top left in portrait (first of six buttons in the top row)
read -r W H < <(size)
adb shell input tap $((W / 12)) $((W / 6 * 62 / 200))
sleep 3
shot portrait-keyboard
adb shell input tap $((W / 12)) $((W / 6 * 62 / 200))
sleep 1

adb shell settings put system user_rotation 1
sleep 8
shot landscape

adb logcat -d > smoke/logcat.txt
echo "=== logcat (app, SDL, crashes)"
grep -E "C64-EmuDis|SDL|VICE|AndroidRuntime|DEBUG|libc|$PKG" smoke/logcat.txt | tail -n 150
if adb shell pidof $PKG > /dev/null; then
    echo "=== app is running"
else
    echo "=== APP IS NOT RUNNING"
    exit 1
fi
