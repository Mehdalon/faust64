#!/usr/bin/env python3
"""vprep.py SRC.wav LEVELS.bin SCX.bin [scx] - the sound program's render -> mkgbv.py inputs.

Left channel: the sound, one sample per screen line (9198 Hz), encoded to the 8
master-volume levels as gb/encode.py does (exactly, if it is already on them).
Right channel, only if the program says `// faust2gb: scx`: the horizontal
scroll of each line, in pixels / 128 (so -1..1 = -128..128 pixels), written
per line by the player - raster effects from the same program as the sound.
"""
import struct, sys, wave, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from encode import encode
w = wave.open(sys.argv[1]); n = w.getnframes(); ch = w.getnchannels()
a = struct.unpack("<%dh" % (n * ch), w.readframes(n))
left = [a[i * ch] / 32768.0 for i in range(n)]
data, mode = encode(left)
open(sys.argv[2], "wb").write(bytes(b // 0x11 for b in data))
use = len(sys.argv) > 4 and sys.argv[4] == "scx" and ch > 1
scx = bytes((int(round(a[i * ch + 1] / 32000.0 * 128)) & 0xFF) if use else 0 for i in range(n))
open(sys.argv[3], "wb").write(scx)
print(f"vprep: {n} lines of sound ({n / w.getframerate():.1f} s), 3-bit, {mode}; scroll per line: {'from the right channel' if use else 'none'}")
