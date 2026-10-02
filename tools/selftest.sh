#!/usr/bin/env bash
# selftest.sh FILE.dsp [SR]
#
# Renders 4096 samples of the DSP on the host from Faust's generated C, then
# builds a ROM that renders the same 4096 samples on the N64 and prints its
# fingerprint on screen. Run the ROM and compare the three numbers by eye.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Which faust? Homebrew's aborts on startup here (its LLVM wants libz3.4.16,
# which brew cleanup removed, and Homebrew refuses to rebuild LLVM on this
# machine - "Tier 3 configuration"). Fall back to the locally built compiler.
FAUST="${FAUST:-faust}"
( "$FAUST" --version >/dev/null 2>&1 ) || { echo "$(basename "$0"): no working faust" >&2; exit 1; }

DSP="${1:?usage: selftest.sh FILE.dsp [SR]}"
SR="${2:-22050}"
BASE="$(basename "$DSP" .dsp)"
IDENT="$(printf '%s' "$BASE" | tr -c 'A-Za-z0-9_' '_')"
WORK="$HERE/build/selftest-$IDENT"
FAUSTINC="$("$FAUST" --includedir 2>/dev/null || echo /usr/local/include)"
[ -d "$FAUSTINC/faust" ] || FAUSTINC=/usr/local/include

rm -rf "$WORK"; mkdir -p "$WORK"
"$FAUST" -lang c -ftz 1 -cn "$IDENT" -o "$WORK/gen.c" "$DSP"
cc -O2 -std=gnu11 -I"$FAUSTINC" -I"$WORK" -DHOST_SR="$SR" \
   -o "$WORK/ref" "$HERE/tools/host_ref.c" -lm
echo "--- host reference (${SR} Hz, 4096 samples) ---"
"$WORK/ref"
echo "--- building the N64 self-test ROM ---"
"$HERE/faust2n64" -S -r "$SR" -o "$WORK/$IDENT-selftest.z64" "$DSP" >/dev/null
echo "ROM: $WORK/$IDENT-selftest.z64"
echo "Run it and compare sum/min/max with the host numbers above."
