#!/bin/bash
# Adds C64-EmuDis to the application menu of the current user.
# Run it from the extracted C64-EmuDis folder.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
ICONS="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/256x256/apps"
mkdir -p "$APPS" "$ICONS"
cp "$HERE/c64-emudis.png" "$ICONS/c64-emudis.png"
sed "s|^Exec=.*|Exec=\"$HERE/C64-EmuDis\" %f|" "$HERE/c64-emudis.desktop" > "$APPS/c64-emudis.desktop"
echo "C64-EmuDis was added to your application menu."
