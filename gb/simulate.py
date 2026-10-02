#!/usr/bin/env python3
"""simulate.py ROM.gb SRC.wav PREVIEW.wav [SECONDS] - run the ROM's player, check it.

A tiny SM83 (Game Boy CPU) interpreter for exactly the instructions gb/mkgb.py
emits, with M-cycle counts, the MBC5 bank register and the sound registers. It
runs the ROM from $0150, records every NR50 write with its cycle time, rebuilds
the sound from them, and compares it with the Faust render: sample period, and
correlation / SNR. Also writes what the Game Boy would play, as a WAV.
"""
import sys, struct, wave
sys.path.insert(0, __import__("os").path.dirname(__file__))
from encode import read, SR

def run(rom, max_cycles):
    mem = bytearray(0x10000); bank = 1; regs = dict(a=0, b=0, c=0, d=0, e=0, h=0, l=0); pc = 0x150; cyc = 0; z = False
    writes = []
    def rd(ad):
        if ad < 0x4000: return rom[ad]
        if ad < 0x8000: return rom[bank * 0x4000 + ad - 0x4000]
        return mem[ad]
    def wr(ad, v):
        nonlocal bank
        if 0x2000 <= ad < 0x3000: bank = (bank & 0x100) | v
        elif 0x3000 <= ad < 0x4000: bank = (bank & 0xFF) | ((v & 1) << 8)
        elif ad >= 0x8000:
            mem[ad] = v
            if ad == 0xFF24: writes.append((cyc, v))
    hl = lambda: (regs["h"] << 8) | regs["l"]
    def sethl(v): regs["h"], regs["l"] = (v >> 8) & 0xFF, v & 0xFF
    r8 = {0x06: "b", 0x0E: "c", 0x3E: "a"}
    while cyc < max_cycles:
        op = rd(pc)
        if op == 0xF3: pc += 1; cyc += 1                                  # di
        elif op == 0x00: pc += 1; cyc += 1                                # nop
        elif op == 0x31: pc += 3; cyc += 3                                # ld sp,nn
        elif op in r8: regs[r8[op]] = rd(pc + 1); pc += 2; cyc += 2       # ld r,n
        elif op == 0xE0: wr(0xFF00 + rd(pc + 1), regs["a"]); pc += 2; cyc += 3   # ldh [n],a
        elif op == 0x21: sethl(rd(pc + 1) | (rd(pc + 2) << 8)); pc += 3; cyc += 3
        elif op == 0x22: wr(hl(), regs["a"]); sethl(hl() + 1); pc += 1; cyc += 2
        elif op == 0x2A: regs["a"] = rd(hl()); sethl(hl() + 1); pc += 1; cyc += 2
        elif op == 0x05: regs["b"] = (regs["b"] - 1) & 0xFF; z = regs["b"] == 0; pc += 1; cyc += 1
        elif op == 0x0C: regs["c"] = (regs["c"] + 1) & 0xFF; z = regs["c"] == 0; pc += 1; cyc += 1
        elif op == 0x20:                                                  # jr nz,e
            e = rd(pc + 1); e = e - 256 if e > 127 else e
            if not z: pc += 2 + e; cyc += 3
            else: pc += 2; cyc += 2
        elif op == 0x18: e = rd(pc + 1); pc += 2 + (e - 256 if e > 127 else e); cyc += 3
        elif op == 0x79: regs["a"] = regs["c"]; pc += 1; cyc += 1
        elif op == 0x7C: regs["a"] = regs["h"]; pc += 1; cyc += 1
        elif op == 0xFE: z = regs["a"] == rd(pc + 1); pc += 2; cyc += 2   # cp n (only Z is used)
        elif op == 0xAF: regs["a"] = 0; z = True; pc += 1; cyc += 1
        elif op == 0xEA: wr(rd(pc + 1) | (rd(pc + 2) << 8), regs["a"]); pc += 3; cyc += 4
        else: sys.exit(f"simulate: unknown opcode ${op:02X} at ${pc:04X}")
    return writes

if __name__ == "__main__":
    rom = open(sys.argv[1], "rb").read()
    src, sr = read(sys.argv[2]); secs = float(sys.argv[4]) if len(sys.argv) > 4 else len(src) / SR
    writes = run(rom, int(secs * 1048576))[1:]                          # [0]: the init's NR50 = 0
    gaps = [b[0] - a[0] for a, b in zip(writes, writes[1:])]
    from collections import Counter
    print("simulate: sample periods (M-cycles):", dict(Counter(gaps).most_common(3)),
          f"-> {1048576 / Counter(gaps).most_common(1)[0][0]:.1f} Hz")
    out = [((v & 7) + 1) / 8.0 for _, v in writes]                      # NR50 level -> output
    n = min(len(out), len(src))
    m = sum(out[:n]) / n; a = [o - m for o in out[:n]]
    ms = sum(src[:n]) / n; b = [s - ms for s in src[:n]]
    num = sum(x * y for x, y in zip(a, b)); den = (sum(x * x for x in a) * sum(y * y for y in b)) ** 0.5
    corr = num / den if den else 0.0
    g = num / max(1e-12, sum(x * x for x in a))
    err = sum((y - g * x) ** 2 for x, y in zip(a, b)); sig = sum(y * y for y in b)
    import math
    print(f"simulate: {n} samples compared, correlation {corr:.4f}, SNR {10 * math.log10(sig / max(1e-12, err)):.1f} dB")
    w = wave.open(sys.argv[3], "wb"); w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes(struct.pack("<%dh" % len(a), *[int(max(-1, min(1, x * 3.2)) * 32000) for x in a])); w.close()
