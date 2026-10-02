#!/usr/bin/env python3
"""simv.py ROM.gb LEVELS.bin SCX.bin PICS.bin [FRAMES] [OUT_PREFIX] - check a faust2gb -v ROM.

Runs the ROM from $0150 on a small SM83 model (the instructions mkgbv.py emits,
with M-cycle counts) plus a model of the screen: LY and the modes advance with
the cycle count, VBlank wakes the halt, writes to video RAM during mode 3 are
counted as LOST (and dropped, as the hardware does), and every visible line is
drawn from video RAM and the scroll/palette registers as they are when the line
starts. Then it checks, against the inputs mkgbv.py was given:
  - sound: every NR50 write 114 M-cycles after the last, values = the encoded sound;
  - picture: every drawn line equals the Faust picture (shifted by that line's
    SCX) that should be on screen in that frame - no lost writes, no tearing;
  - the margins: how close the video-RAM writes came to mode 3.
Mode 3 is taken as M-cycles 20..65 of a line (172 dots + up to 7 for SCX & 7,
rounded up); hardware/emulator latencies differ by a cycle or two - see the margins.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkgbv as G
LINE = G.LINE

def run(rom, frames):
    mem = bytearray(0x10000); bank = 1
    R = dict(a=0, b=0, c=0, d=0, e=0, h=0, l=0); sp = 0xFFFE; pc = 0x150; cyc = 0; z = False
    ime = False; lcd_on = True; c0 = 0                       # the boot ROM leaves the LCD on
    nr50, lost, vram_t, screens = [], [], [], []
    cur = [[0] * 160 for _ in range(144)]; next_draw = None; synced = None

    def line_t(c): return divmod((c - c0) % G.FRAME, LINE)
    def rd(ad):
        if ad < 0x4000: return rom[ad]
        if ad < 0x8000: return rom[bank * 0x4000 + ad - 0x4000]
        if ad == 0xFF44: return line_t(cyc)[0] if lcd_on else 0
        return mem[ad]
    def wr(ad, v, at):
        nonlocal bank, lcd_on, c0
        if 0x2000 <= ad < 0x3000: bank = (bank & 0x100) | v; return
        if 0x3000 <= ad < 0x4000: bank = (bank & 0xFF) | ((v & 1) << 8); return
        if ad < 0x8000: return
        if 0x8000 <= ad < 0xA000 and lcd_on:
            L, t = line_t(at)
            if L < 144 and 20 <= t < 66: lost.append((at, ad)); return
            if synced is not None: vram_t.append(t)
        if ad == 0xFF40:
            if v & 0x80 and not lcd_on: c0 = at
            lcd_on = bool(v & 0x80)
        if ad == 0xFF24 and synced is not None: nr50.append((at, v))
        mem[ad] = v
    def draw(L):
        lcdc, scx, scy, bgp = mem[0xFF40], mem[0xFF43], mem[0xFF42], mem[0xFF47]
        mp = 0x9C00 if lcdc & 8 else 0x9800; py = (L + scy) & 255; row = cur[L]
        for x in range(160):
            px = (x + scx) & 255; i = mem[mp + (py >> 3) * 32 + (px >> 3)]
            a = 0x8000 + i * 16 if lcdc & 0x10 else 0x9000 + (i - 256 if i > 127 else i) * 16
            a += (py & 7) * 2; bit = 7 - (px & 7)
            c = ((mem[a] >> bit) & 1) | (((mem[a + 1] >> bit) & 1) << 1)
            row[x] = 3 - ((bgp >> (2 * c)) & 3)        # brightness: 0 black .. 3 white
    rr = lambda r1, r2: (R[r1] << 8) | R[r2]
    def set2(r1, r2, v): R[r1], R[r2] = (v >> 8) & 0xFF, v & 0xFF
    REG = "bcdehl"
    while True:
        # draw every visible line that has reached mode 3 since the last instruction
        if synced is not None:
            while next_draw <= cyc:
                L, _ = line_t(next_draw)
                if L < 144: draw(L)
                if L == 143:
                    screens.append([r[:] for r in cur])
                    if len(screens) >= frames: return nr50, lost, vram_t, screens, synced
                next_draw += LINE
        op = rd(pc)
        if op == 0x00: pc += 1; cyc += 1
        elif op == 0xF3: ime = False; pc += 1; cyc += 1
        elif op == 0xFB: ime = True; pc += 1; cyc += 1
        elif op == 0x76:                                         # halt until VBlank (IE = VBlank only)
            assert ime and mem[0xFFFF] & 1, "halt without the VBlank interrupt"
            L, t = line_t(cyc); wait = ((144 - L) % 154) * LINE - t
            if wait <= 0: wait += G.FRAME
            cyc += wait; vb = cyc
            cyc += 6; ime = False                                # halt exit + dispatch: push PC, jump to $0040
            sp -= 2; pc = 0x40; synced = vb
            L0 = (vb - c0) // LINE                               # draw from line 0 of the next frame on
            next_draw = c0 + (L0 + 10) * LINE + 20
        elif op in (0x31, 0x21, 0x01, 0x11):
            v = rd(pc + 1) | (rd(pc + 2) << 8)
            if op == 0x31: sp = v
            else: set2(*{0x21: "hl", 0x01: "bc", 0x11: "de"}[op], v)
            pc += 3; cyc += 3
        elif op in (0x3E, 0x06, 0x0E): R[{0x3E: "a", 0x06: "b", 0x0E: "c"}[op]] = rd(pc + 1); pc += 2; cyc += 2
        elif op == 0xE0: wr(0xFF00 + rd(pc + 1), R["a"], cyc + 2); pc += 2; cyc += 3
        elif op == 0xF0: cyc += 2; R["a"] = rd(0xFF00 + rd(pc + 1)); pc += 2; cyc += 1
        elif op == 0xEA: wr(rd(pc + 1) | (rd(pc + 2) << 8), R["a"], cyc + 3); pc += 3; cyc += 4
        elif op == 0x22: hl = rr("h", "l"); wr(hl, R["a"], cyc + 1); set2("h", "l", hl + 1); pc += 1; cyc += 2
        elif op == 0x1A: R["a"] = rd(rr("d", "e")); pc += 1; cyc += 2
        elif op == 0x13: set2("d", "e", rr("d", "e") + 1); pc += 1; cyc += 2
        elif op == 0x0B: set2("b", "c", rr("b", "c") - 1); pc += 1; cyc += 2
        elif 0x78 <= op <= 0x7D: R["a"] = R[REG[op - 0x78]]; pc += 1; cyc += 1
        elif op == 0xB1: R["a"] |= R["c"]; z = R["a"] == 0; pc += 1; cyc += 1
        elif op == 0xAF: R["a"] = 0; z = True; pc += 1; cyc += 1
        elif op == 0x3D: R["a"] = (R["a"] - 1) & 0xFF; z = R["a"] == 0; pc += 1; cyc += 1
        elif op == 0x05: R["b"] = (R["b"] - 1) & 0xFF; z = R["b"] == 0; pc += 1; cyc += 1
        elif op == 0xFE: z = R["a"] == rd(pc + 1); pc += 2; cyc += 2
        elif op == 0x20:
            e = rd(pc + 1); e -= 256 if e > 127 else 0
            if not z: pc += 2 + e; cyc += 3
            else: pc += 2; cyc += 2
        elif op == 0x18:
            e = rd(pc + 1); pc += 2 + (e - 256 if e > 127 else e); cyc += 3
        elif op == 0xC3: pc = rd(pc + 1) | (rd(pc + 2) << 8); cyc += 4
        elif op in (0xC1, 0xD1, 0xE1):
            r1, r2 = {0xC1: "bc", 0xD1: "de", 0xE1: "hl"}[op]
            R[r2] = rd(sp); R[r1] = rd(sp + 1); sp = (sp + 2) & 0xFFFF; pc += 1; cyc += 3
        elif op == 0xF9: sp = rr("h", "l"); pc += 1; cyc += 2
        else: sys.exit(f"simv: unknown opcode ${op:02X} at ${pc:04X}")

def expected(levels, scx, pics, K):
    """What should come out: sound per line (with the delay), and each frame's screen."""
    pad = G.DELAY * G.LINES
    lv = bytes([levels[0]]) * pad + bytes(levels); sx = bytes([scx[0]]) * pad + bytes(scx)
    n = len(lv); P = G.PW * G.PH
    snd = [lv[i % n] * 0x11 for i in range(K * G.LINES)]
    scr = []
    for k in range(K):
        shown = (k - 2) // 2; pic = pics[shown % len(pics)] if shown >= 0 else bytes(P)
        rows = []
        for L in range(144):
            s = sx[(k * G.LINES + 10 + L) % n]; py = (L + G.SCY) & 255; pr = py - G.ROW0 * 8; row = []
            for x in range(160):
                pcx = ((x + s) & 255) - G.COL0 * 8
                row.append(pic[pr * G.PW + pcx] if 0 <= pr < G.PH and 0 <= pcx < G.PW else 0)
            rows.append(row)
        scr.append(rows)
    return snd, scr

if __name__ == "__main__":
    rom = open(sys.argv[1], "rb").read()
    lev = open(sys.argv[2], "rb").read(); scx = open(sys.argv[3], "rb").read(); raw = open(sys.argv[4], "rb").read()
    P = G.PW * G.PH; pics = [raw[i:i + P] for i in range(0, len(raw) - P + 1, P)]
    F = int(sys.argv[5]) if len(sys.argv) > 5 else 24
    nr50, lost, vt, screens, vb = run(rom, F)
    snd, scr = expected(lev, scx, pics, F)
    gaps = sorted(set(b[0] - a[0] for a, b in zip(nr50, nr50[1:])))
    vals = [v for _, v in nr50]
    sound_ok = gaps == [LINE] and vals[:len(snd)] == snd[:len(vals)]
    bad = sum(1 for k in range(len(screens)) for L in range(144) if screens[k][L] != scr[k][L])
    m = min(min(t - 66 if t >= 66 else t + LINE - 66 for t in vt), min(20 + LINE - 1 - t if t >= 66 else 19 - t for t in vt)) if vt else None
    print(f"simv: {len(screens)} frames, {len(nr50)} sound writes, intervals {gaps} M-cycles "
          f"({G.RATE:.2f} Hz), sound {'= encoded stream' if sound_ok else 'DIFFERS'}")
    print(f"simv: video-RAM writes {len(vt)}, lost in mode 3: {len(lost)}, closest to mode 3: {m} M-cycles; "
          f"lines differing from the Faust picture: {bad} of {len(screens) * 144}")
    if len(sys.argv) > 6:
        try:
            from PIL import Image
            show = [k for k in range(len(screens)) if k >= 2][-4:]
            im = Image.new("L", (160 * len(show), 144))
            for j, k in enumerate(show):
                im.paste(Image.frombytes("L", (160, 144), bytes(85 * s for r in screens[k] for s in r)), (160 * j, 0))
            im.resize((im.width * 2, 288), Image.NEAREST).save(sys.argv[6] + "-frames.png")
            print(f"simv: wrote {sys.argv[6]}-frames.png")
        except ImportError:
            pass
    sys.exit(0 if sound_ok and not lost and bad == 0 else 1)
