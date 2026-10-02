#!/usr/bin/env bash
# fxcheck.sh FILE.dsp [SR] [SAMPLES]
#
# Render a DSP twice on the host - once with Faust's normal float output, once
# with -fx -fx-size 32 through lib/faustfx.h - and compare them. This is the
# correctness test for the fixed-point layer, and it needs no console.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Which faust? Homebrew's aborts on startup here (its LLVM wants libz3.4.16,
# which brew cleanup removed, and Homebrew refuses to rebuild LLVM on this
# machine - "Tier 3 configuration"). Fall back to the locally built compiler.
FAUST="${FAUST:-faust}"
( "$FAUST" --version >/dev/null 2>&1 ) || { echo "$(basename "$0"): no working faust" >&2; exit 1; }

DSP="${1:?usage: fxcheck.sh FILE.dsp [SR] [SAMPLES]}"
SR="${2:-16000}"
N="${3:-16000}"
# Use the patched compiler when it is built: the stock one emits a table and
# its fill function with different types, which does not compile at all.
FAUSTBIN="${FAUSTBIN:-${FAUST:-faust}}"
[ -x "$FAUSTBIN" ] || FAUSTBIN=faust
FAUSTLIB="${FAUSTLIB:-$("${FAUST:-faust}" --libdir 2>/dev/null)}"
FAUSTINC="$("$FAUST" --includedir 2>/dev/null || echo /usr/local/include)"
[ -d "$FAUSTINC/faust" ] || FAUSTINC=/usr/local/include

BASE="$(basename "$DSP" .dsp)"
IDENT="$(printf '%s' "$BASE" | tr -c 'A-Za-z0-9_' '_')"
W="$HERE/build/fxcheck-$IDENT"
rm -rf "$W"; mkdir -p "$W"
cp "$HERE/lib/faustfx.h" "$W/faustfx.h"

# float reference
"$FAUSTBIN" -I "$FAUSTLIB" -lang c -ftz 1 -cn "$IDENT" -o "$W/gen.c" "$DSP"
c++ -O2 -I"$FAUSTINC" -I"$W" -DCHK_SR="$SR" -o "$W/ref" "$HERE/tools/fxcheck.cpp" -lm
"$W/ref" "$N" "$W/ref.f32"

# fixed point through our layer
"$FAUSTBIN" -I "$FAUSTLIB" -lang c -fx -fx-size 32 -cn "$IDENT" -o "$W/gen.c" "$DSP"
c++ -O2 -std=c++14 -I"$FAUSTINC" -I"$W" -DCHK_SR="$SR" -DCHK_FIXED \
    -o "$W/fix" "$HERE/tools/fxcheck.cpp" -lm
"$W/fix" "$N" "$W/fix.f32"

python3 - "$W/ref.f32" "$W/fix.f32" <<'PY'
import sys, struct, math
a=open(sys.argv[1],'rb').read(); b=open(sys.argv[2],'rb').read()
n=min(len(a),len(b))//4
A=struct.unpack("<%df"%n, a[:n*4]); B=struct.unpack("<%df"%n, b[:n*4])
if n==0: print("no samples"); sys.exit(1)
ra=math.sqrt(sum(x*x for x in A)/n); rb=math.sqrt(sum(x*x for x in B)/n)
err=[x-y for x,y in zip(A,B)]
re_=math.sqrt(sum(e*e for e in err)/n)
peak=max(abs(e) for e in err)
den=math.sqrt(sum(x*x for x in A))*math.sqrt(sum(y*y for y in B))
corr=(sum(x*y for x,y in zip(A,B))/den) if den>0 else 0.0
snr=20*math.log10(ra/re_) if re_>0 and ra>0 else float('inf')
print("  samples      %d" % n)
print("  rms  float   %.6f" % ra)
print("  rms  fixed   %.6f" % rb)
print("  correlation  %.6f" % corr)
print("  error rms    %.6g   peak %.6g" % (re_, peak))
print("  SNR          %.1f dB" % snr)
PY
