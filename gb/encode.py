#!/usr/bin/env python3
"""encode.py SRC.wav OUT.bin - Faust's audio -> Game Boy master-volume PCM.

The Game Boy has no sample DAC. The player (gb/mkgb.py) keeps the wave channel
outputting a constant level and rewrites the master volume NR50 every sample:
8 levels, output proportional to (v + 1) / 8 - linear in v, so 3 bits of PCM.
One byte per sample, v in both nibbles (left and right volume), bit 3 and bit 7
clear (no cartridge audio in). Error diffusion moves the quantisation noise up
out of the way, as in zx/encode.py. A signal already on the 8 levels (see
demos/chip.dsp) is passed through as it is: no gain change, no added noise.
"""
import struct, sys, wave
SR = 8192            # 1048576 M-cycles/s / 128 per sample - counted in mkgb.py
LEVELS = 8

def read(path):
    w = wave.open(path); n = w.getnframes(); ch = w.getnchannels()
    assert w.getsampwidth() == 2, "16-bit WAV expected"
    a = struct.unpack("<%dh" % (n * ch), w.readframes(n))
    x = [sum(a[i * ch:(i + 1) * ch]) / (ch * 32768.0) for i in range(n)]
    return x, w.getframerate()

def level(v):
    return (v + 1.0) * 0.5 * (LEVELS - 1)

def on_grid(x):
    return all(abs(level(v) - round(level(v))) < 0.02 for v in x)

def encode(x):
    for fs in (1.0, 32768.0 / 32000.0):   # faustlab writes full scale as 32000 (host_render.c)
        y = [v * fs for v in x]
        if on_grid(y):   # already 3-bit (e.g. pulse waves on the 8 levels): pass through, no added noise
            return bytearray(max(0, min(LEVELS - 1, int(round(level(v))))) * 0x11 for v in y), "exact"
    peak = max(1e-9, max(abs(v) for v in x)); x = [v * 0.98 / peak for v in x]
    out, err = bytearray(), 0.0
    for v in x:
        want = (v + 1.0) * 0.5 * (LEVELS - 1) + err
        q = max(0, min(LEVELS - 1, int(round(want))))
        err = want - q
        out.append(q * 0x11)
    return out, "error diffusion"

if __name__ == "__main__":
    x, sr = read(sys.argv[1])
    if sr != SR: sys.exit(f"encode: {sys.argv[1]} is {sr} Hz, expected {SR}")
    data, mode = encode(x); open(sys.argv[2], "wb").write(data)
    print(f"encode: {len(data)} samples, {len(data) / SR:.1f} s at {SR} Hz, 3-bit, {mode}")
