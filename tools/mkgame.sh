#!/usr/bin/env bash
# mkgame.sh [RATE] - build game/demo.z64: what faust64.h looks like in a game.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Which faust: $FAUST if set, else `faust` from PATH.
FAUST="${FAUST:-faust}"
( "$FAUST" --version >/dev/null 2>&1 ) 2>/dev/null || { echo "$(basename "$0"): no working faust" >&2; exit 1; }

SR="${1:-16000}"
: "${N64_INST:=$HOME/n64-toolchain}"
export N64_INST
[ -f "$N64_INST/include/n64.mk" ] || { echo "mkgame: no toolchain at $N64_INST" >&2; exit 1; }
FAUSTINC="$("$FAUST" --includedir 2>/dev/null || echo /usr/local/include)"
[ -d "$FAUSTINC/faust" ] || FAUSTINC=/usr/local/include

MUSIC="${MUSIC:-demos/arp.dsp}"
W="$HERE/build/game"
rm -rf "$W"; mkdir -p "$W"
cp "$HERE/game/demo.c" "$W/demo.c"
cp "$HERE/game/faust64.h" "$W/faust64.h"
cp "$HERE/ui/faustui.h" "$W/faustui.h"

"$FAUST" -lang c -ftz 1 -cn e_music  -o "$W/e_music.c"  "$HERE/$MUSIC"
"$FAUST" -lang c -ftz 1 -cn e_sfx    -o "$W/e_sfx.c"    "$HERE/game/sfx.dsp"
"$FAUST" -lang c -ftz 1 -cn m_master -o "$W/m_master.c" "$HERE/game/master.dsp"

cat > "$W/Makefile" <<MK
BUILD_DIR = obj
include \$(N64_INST)/include/n64.mk
N64_C_AND_CXX_FLAGS += -I$FAUSTINC -DGAME_SR=$SR
N64_C_AND_CXX_FLAGS += -include faust/gui/CInterface.h
N64_C_AND_CXX_FLAGS += -Wno-error=unused-but-set-variable -Wno-error=unused-function
all: demo.z64
.PHONY: all
OBJS = \$(BUILD_DIR)/demo.o \$(BUILD_DIR)/e_music.o \$(BUILD_DIR)/e_sfx.o \$(BUILD_DIR)/m_master.o
\$(BUILD_DIR)/demo.elf: \$(OBJS)
demo.z64: N64_ROM_TITLE = "FAUST64 GAME"
MK
make -C "$W" -s
cp "$W/demo.z64" "$HERE/game/demo.z64"
echo "mkgame: game/demo.z64 @ ${SR} Hz (music = $MUSIC)"
