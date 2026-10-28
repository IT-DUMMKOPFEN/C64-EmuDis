#!/bin/bash
#
# selftest.sh - run the C64-EmuDis self test (video recording + autostart)
#
# Usage: selftest.sh <label> <log native> <mp4 native> <prg native> -- <command> [args]
#
# The paths are passed to the emulator as given ("native": Windows paths on
# Windows). The emulator is killed after 90 seconds; that counts as a freeze.
# Results are printed as GitHub annotations.
#
# This file is part of C64-EmuDis (GPL-2.0-or-later).

label="$1"; log="$2"; mp4="$3"; prg="$4"
shift 4
[ "$1" = "--" ] && shift

export EMUDIS_SELFTEST_RECORD="$mp4"
export EMUDIS_SELFTEST_DROP="$prg"

posix() { if command -v cygpath >/dev/null 2>&1; then cygpath -u "$1"; else echo "$1"; fi; }
plog=$(posix "$log")
pmp4=$(posix "$mp4")
rm -f "$plog" "$pmp4"

# -default must come first, it resets everything given before it
exe="$1"; shift
"$exe" -default -logfile "$log" "$@" > "$plog.stdout" 2>&1 &
pid=$!
status="finished"
for i in $(seq 1 90); do
    if ! kill -0 "$pid" 2>/dev/null; then
        break
    fi
    sleep 1
done
if kill -0 "$pid" 2>/dev/null; then
    status="FROZEN (killed after 90 s)"
    kill -9 "$pid" 2>/dev/null
    command -v taskkill >/dev/null 2>&1 && taskkill //F //IM C64-EmuDis.exe >/dev/null 2>&1
    pkill -9 -f x64sc 2>/dev/null
fi
wait "$pid" 2>/dev/null

size=0
[ -f "$pmp4" ] && size=$(wc -c < "$pmp4" | tr -d ' ')
summary="$label: $status, video size $size bytes"
if [ "$status" = "finished" ] && [ "$size" -gt 100000 ]; then
    echo "::notice title=selftest $label::$summary"
else
    echo "::warning title=selftest $label::$summary"
fi
if [ -f "$plog" ]; then
    msg=$(grep -E "SELFTEST|FFMPEG|ffmpeg|Coproc|Sound|sound|rror|arning" "$plog" | grep -v "t=[0-9]*s clk" | tail -n 60 \
          | sed -e 's/%/%25/g' -e 's/\r//g' | awk 'BEGIN{ORS="%0A"}{print}')
    beats=$(grep -E "t=[0-9]+s clk" "$plog" | tail -n 25 | sed -e 's/\r//g' | awk 'BEGIN{ORS="%0A"}{print}')
    echo "::warning title=selftest $label log::$msg"
    echo "::warning title=selftest $label heartbeat::$beats"
    fflog=$(sed -n "s/.*ffmpeg messages go to '\(.*\)'.*/\1/p" "$plog" | tail -n 1 | tr -d '\r')
    if [ -n "$fflog" ]; then
        pff=$(posix "$fflog")
        if [ -s "$pff" ]; then
            msg=$(tail -n 40 "$pff" | sed -e 's/%/%25/g' -e 's/\r//g' | awk 'BEGIN{ORS="%0A"}{print}')
            echo "::warning title=selftest $label ffmpeg.log::$msg"
        else
            echo "::notice title=selftest $label ffmpeg.log::(empty)"
        fi
    fi
else
    echo "::warning title=selftest $label log::no log file ($plog)"
fi
exit 0
