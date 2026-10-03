#!/usr/bin/env bash
# play.sh FILE - launch the right emulator for a build, with the settings that
# were actually established by testing rather than by guessing.
#
#   tools/play.sh demos/acid.z64        Nintendo 64   -> ares
#   tools/play.sh zx/out/acid.tap       ZX Spectrum   -> Spectral
#   tools/play.sh examples/sine.dol     GameCube/Wii  -> Dolphin
#   tools/play.sh demos/chip.gb         Game Boy      -> SameBoy
#   tools/play.sh demos/acid.exe        MS-DOS        -> DOSBox (dosbox-staging)
#
# Env overrides:  ARES=  SPECTRAL=  DOLPHIN=  SAMEBOY=  DOSBOX=  CYCLES=  LATENCY=  KEEP=1 (don't kill
# a running instance)  DRY=1 (print the command and exit)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FILE="${1:?usage: play.sh FILE.[z64|tap|tzx|sna|z80|dol|elf|gb|exe]}"
[ -f "$FILE" ] || { echo "play.sh: no such file: $FILE" >&2; exit 1; }
shift || true
FILE="$(cd "$(dirname "$FILE")" && pwd)/$(basename "$FILE")"   # absolute

ARES="${ARES:-/Applications/ares.app/Contents/MacOS/ares}"
SPECTRAL="${SPECTRAL:-/Applications/Spectral.app}"
DOLPHIN="${DOLPHIN:-/Applications/Dolphin.app/Contents/MacOS/Dolphin}"
LATENCY="${LATENCY:-80}"
SAMEBOY="${SAMEBOY:-/Applications/SameBoy.app}"
DOSBOX="${DOSBOX:-dosbox-staging}"

run() {
    if [ "${DRY:-0}" = 1 ]; then printf '%q ' "$@"; echo; exit 0; fi
    "$@" >/tmp/faust64-emu.log 2>&1 &
    sleep 1
    echo "play.sh: launched, log at /tmp/faust64-emu.log"
}

kill_prev() {
    [ "${KEEP:-0}" = 1 ] && return 0
    pkill -f "$1" 2>/dev/null || true
    sleep 1
}

case "${FILE##*.}" in

z64|n64|v64|rom)
    # ---- Nintendo 64, via ares -------------------------------------------
    # Audio/Dynamic=true: the N64's AI runs at 15997 Hz, not 16000 - the
    #   hardware divides a 48681812 Hz clock and no exact rate exists. Without
    #   dynamic rate control ares cannot absorb that drift and a short buffer
    #   periodically over- or underflows, which clicks. This audibly helped.
    # Audio/Latency: more slack for the same reason. 80 ms by default.
    # Input/Defocus=Allow: ares otherwise PAUSES whenever its window loses
    #   focus, which is indistinguishable from a hung ROM. This cost an hour
    #   twice before it was understood.
    [ -x "$ARES" ] || { echo "play.sh: no ares at $ARES" >&2; exit 1; }
    kill_prev "Contents/MacOS/ares"
    run "$ARES" \
        --setting Input/Defocus=Allow \
        --setting Audio/Dynamic=true \
        --setting Audio/Latency="$LATENCY" \
        --system "Nintendo 64" --no-file-prompt "$FILE" "$@"
    ;;

tap|tzx|pzx|csw|sna|z80|szx)
    # ---- ZX Spectrum, via Spectral ---------------------------------------
    # Spectral bundles the Sinclair ROMs, so nothing has to be fetched.
    # It boots a 128K to its menu: a 48K tape needs "Tape Loader".
    # Its app bundle ships with NO CFBundleIdentifier, which makes the window
    # unaddressable by tooling; one is added here if missing.
    [ -d "$SPECTRAL" ] || { echo "play.sh: no Spectral at $SPECTRAL" >&2; exit 1; }
    if ! /usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" \
            "$SPECTRAL/Contents/Info.plist" >/dev/null 2>&1; then
        /usr/libexec/PlistBuddy -c \
            "Add :CFBundleIdentifier string org.spectral.emulator" \
            "$SPECTRAL/Contents/Info.plist" >/dev/null 2>&1 || true
        touch "$SPECTRAL"
        echo "play.sh: added a bundle id to Spectral so its window can be addressed"
    fi
    kill_prev "Spectral"
    if [ "${DRY:-0}" = 1 ]; then echo "open -a $SPECTRAL --args $FILE"; exit 0; fi
    open -a "$SPECTRAL" --args "$FILE"
    echo "play.sh: launched Spectral. On the 128K menu choose 'Tape Loader'."
    echo "play.sh: note it may open on a second display - check there if you cannot see it."
    ;;

dol|elf|gcm|iso|rvz|wbfs)
    # ---- GameCube / Wii, via Dolphin -------------------------------------
    [ -x "$DOLPHIN" ] || { echo "play.sh: no Dolphin at $DOLPHIN" >&2; exit 1; }
    kill_prev "Dolphin"
    run "$DOLPHIN" -e "$FILE" "$@"
    ;;

gb|gbc)
    # ---- Game Boy, via SameBoy (accurate sound chip and screen timing) ----
    # macOS: the app bundle; elsewhere set SAMEBOY to the sameboy binary.
    if [ -d "$SAMEBOY" ]; then run open -a "$SAMEBOY" "$FILE"
    else command -v "$SAMEBOY" >/dev/null || { echo "play.sh: no SameBoy at $SAMEBOY (set SAMEBOY=)" >&2; exit 1; }
         run "$SAMEBOY" "$FILE"; fi
    ;;

exe|EXE)
    # ---- MS-DOS, via dosbox-staging: a Sound Blaster 16 at A220 I7 D1 H5, the
    # file's folder as C:, the program started at once. CYCLES: emulated CPU
    # speed (default max - the fastest this Mac can emulate).
    command -v "$DOSBOX" >/dev/null || { echo "play.sh: no DOSBox ($DOSBOX); brew install dosbox-staging" >&2; exit 1; }
    D="$(dirname "$FILE")"; P="$(basename "$FILE")"
    run "$DOSBOX" --noprimaryconf --nolocalconf \
        --set sbtype=sb16 --set sbbase=220 --set irq=7 --set dma=1 --set hdma=5 \
        --set "cpu_cycles=${CYCLES:-max}" \
        -c "mount c \"$D\"" -c "c:" -c "$P" "$@"
    ;;

*)
    echo "play.sh: don't know how to run '${FILE##*.}'" >&2
    echo "         known: z64 n64 v64 | tap tzx pzx csw sna z80 szx | dol elf gcm iso | gb | exe" >&2
    exit 2
    ;;
esac
