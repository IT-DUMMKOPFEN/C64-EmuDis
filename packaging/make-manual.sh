#!/bin/bash
#
# make-manual.sh - build the VICE manual as one HTML file for the packages
#
# Usage: make-manual.sh <source dir> <output file>
#
# Needs makeinfo (texinfo). Never fails the build: without makeinfo, or if
# the conversion fails, no manual is bundled and C64-EmuDis opens the online
# manual instead.
#
# This file is part of C64-EmuDis (GPL-2.0-or-later).

SRC="$1"
OUT="$2"

MAKEINFO=""
# macOS: prefer Homebrew's texinfo (keg-only), /usr/bin/makeinfo is ancient
if command -v brew >/dev/null 2>&1; then
    p="$(brew --prefix texinfo 2>/dev/null)/bin/makeinfo"
    [ -x "$p" ] && MAKEINFO="$p"
fi
[ -z "$MAKEINFO" ] && MAKEINFO=$(command -v makeinfo || command -v texi2any || true)
if [ -z "$MAKEINFO" ]; then
    echo "WARNING: makeinfo not found, the VICE manual is not bundled"
    [ -n "$GITHUB_ACTIONS" ] && echo "::warning title=VICE manual not bundled::makeinfo not found"
    exit 0
fi

mkdir -p "$(dirname "$OUT")"
work=$(mktemp -d 2>/dev/null || echo "${TMPDIR:-/tmp}/vicemanual.$$")
mkdir -p "$work"
cp "$SRC/doc/vice.texi" "$SRC/doc/gpl.texi" "$work/"

if (cd "$work" && "$MAKEINFO" --html --no-split --no-validate --force \
        -o vice-manual.html vice.texi > makeinfo.log 2>&1) \
        && [ -s "$work/vice-manual.html" ]; then
    cp "$work/vice-manual.html" "$OUT"
    echo "VICE manual: $OUT ($(wc -c < "$OUT") bytes)"
    [ -n "$GITHUB_ACTIONS" ] && echo "::notice title=VICE manual::bundled, $(wc -c < "$OUT") bytes ($("$MAKEINFO" --version | head -n 1))"
else
    echo "WARNING: makeinfo failed, the VICE manual is not bundled:"
    tail -n 20 "$work/makeinfo.log" 2>/dev/null
    if [ -n "$GITHUB_ACTIONS" ]; then
        msg=$(tail -n 20 "$work/makeinfo.log" 2>/dev/null | sed -e 's/%/%25/g' -e 's/\r//g' | awk 'BEGIN{ORS="%0A"}{print}')
        echo "::warning title=VICE manual not bundled::$msg"
    fi
fi
rm -rf "$work"
exit 0
