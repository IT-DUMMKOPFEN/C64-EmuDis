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
set -x

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
    ls -la "smoke/$1.png"
    { set +x; } 2>/dev/null
    base64 -w 2000 "smoke/$1.png"
    echo "=== SCREENSHOT $1 END"
    set -x
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
adb logcat -d -v brief "VICE:V" "EmuDisKey:V" "SDL:V" "C64-EmuDis:V" "libc:V" "DEBUG:V" "AndroidRuntime:V" "*:F" | tail -n 250
shot portrait

# KEYS button: top left in portrait (first of six buttons in the top row)
read -r W H < <(size)
adb shell input tap $((W / 12)) $((W / 6 * 62 / 200))
sleep 3
# type HI on the C64 keyboard (layout as in EmuActivity/C64KeyboardView)
python3 - "$W" "$H" > smoke/keys.txt <<'PY'
import sys
w, h = float(sys.argv[1]), float(sys.argv[2])
bw = w / 6; bar = min(bw * 0.62, h * 0.08); m = bw * 0.05
top = bar + m + min(w / 1.32, h * 0.45)
kh = min(h - top, w * 5 / 18 * 1.6); uw = w / 18; uh = kh / 5
for kx, ky in ((7.5, 2.5), (9.0, 1.5), (14.0, 2.5)):   # H, I, RETURN
    print(int(kx * uw), int(top + ky * uh))
PY
while read -r X Y; do
    adb shell input tap "$X" "$Y" < /dev/null
    sleep 1
done < smoke/keys.txt
sleep 8
shot portrait-keyboard
adb shell input tap $((W / 12)) $((W / 6 * 62 / 200))
sleep 1

adb shell settings put system user_rotation 1
sleep 8
shot landscape

adb logcat -d > smoke/logcat.txt
echo "=== logcat (app, SDL, crashes)"
grep -E "EmuDisKey|AndroidRuntime|F DEBUG|F libc" smoke/logcat.txt | tail -n 80
# SDL shows its errors (e.g. a missing library) in a dialog
adb shell uiautomator dump /sdcard/ui.xml > /dev/null 2>&1
adb shell cat /sdcard/ui.xml > smoke/ui.xml 2>/dev/null
if grep -q "SDL Error" smoke/ui.xml; then
    echo "=== SDL ERROR DIALOG:"
    grep -o 'text="[^"]*"' smoke/ui.xml
    exit 1
fi
if adb shell pidof $PKG > /dev/null; then
    echo "=== app is running"
else
    echo "=== APP IS NOT RUNNING"
    exit 1
fi
