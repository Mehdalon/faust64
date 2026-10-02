#!/usr/bin/env python3
"""mkgbv.py LEVELS.bin SCX.bin PICS.bin OUT.gb [TITLE] - Game Boy ROM: Faust sound AND pictures.

Everything is locked to the screen's line clock (no interrupts, no polling):
one line = 114 M-cycles, one frame = 154 lines = 17556 M-cycles = 59.73 Hz.

Per line the player
  - writes one sound sample to the master volume NR50 (3-bit PCM, as faust2gb):
    one sample per line = 1048576 / 114 = 9198.04 Hz, always at the same point;
  - copies 10 bytes of picture into video RAM, only while the screen is not
    reading it (after mode 3 of this line, before mode 3 of the next);
  - writes the horizontal scroll SCX of the next line (raster effects: the
    sound program's second output can bend the picture line by line).
Each VBlank it reads the next frame's header (screen control, palette, scroll,
where the picture bytes go) and switches the cartridge bank if needed - the
same instructions every frame, so every frame takes exactly the same time.

Picture: 128 x 88 pixels, 4 shades (16 x 11 tiles), double-buffered: picture p
is written over 2 frames into the buffer not on screen, then shown - 29.86
pictures a second, never half-drawn. Buffer A = tiles $8000-$8AFF (map $9800,
LCDC $91), buffer B = tiles $8C00-$96FF (map $9C00, LCDC $89). Tile 176 ($8B00)
is the black border in both maps.

The stream (MBC5 banks 1..), per frame: head [8 audio, LCDC, BGP, SCY, SCX0,
dest lo, dest hi], 144 lines x [audio, 10 picture bytes, SCX of next line],
tail [2 audio, bank lo, bank hi, next frame lo, hi]. pop reads it (SP = stream).

The kernel is generated below by a tiny assembler that counts M-cycles and
checks, at build time, that every sound write is exactly 114 cycles after the
last and every video-RAM write falls in the safe window. gb/simv.py runs the
ROM on a model of the CPU and screen and checks the same against the Faust renders.

The window, measured (2026-10-02): test ROMs with the line-block start T0 moved
(GBV_T0=..., a still picture where a lost byte shows as a dark speck). SameBoy:
T0 = 50 and 88 corrupt, 54 / 68 / 84 clean - so it accepts about 51..86.
gb/simv.py is slightly stricter (55..82). T0 = 68 is the middle: ~14 M-cycles of
room either side for emulators and consoles that differ by a cycle or two.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkgb import LOGO, BANK

LINE, LINES, VIS = 114, 154, 144
FRAME = LINE * LINES                       # 17556
RATE = 1048576 / LINE                      # 9198.04 sound samples a second
FPS = 1048576 / FRAME                      # 59.7275 frames a second
PW, PH = 128, 88                           # picture
TILES = (PW // 8) * (PH // 8)              # 176
PBYTES = TILES * 16                        # 2816
PER_LINE = 10                              # picture bytes per line
FBYTES = PER_LINE * VIS                    # 1440 per frame; 2 frames = 2880 = picture + 4 blank tiles
BUF = (0x8000, 0x8C00); LCDC = (0x91, 0x89); MAP = (0x9800, 0x9C00)
BORDER = 0xB0                              # tile 176 at $8B00: index $B0 in both addressing modes
BGP = 0x1B                                 # colour c -> brightness c (0 black .. 3 white)
SCY = 4                                    # picture rows 4..14 -> screen lines 28..115 (centred)
COL0, ROW0 = 2, 4
T0 = int(os.environ.get("GBV_T0", 68))     # the line-block starts 68 M-cycles into its line (GBV_T0: timing tests only)
LATENCY = 11                               # VBlank flag -> first instruction at `sync` (halt exit + dispatch + jp)
SAFE = (66, 133)                           # video RAM writable from here to here (next line t = 19)
HEAD, LINEB, TAIL = 14, 12, 6
CHUNK = HEAD + VIS * LINEB + TAIL          # 1748 bytes a frame
DELAY = 3                                  # frames of sound delay: the picture shown at a moment was drawn 3 frames earlier

class Asm:
    """Emit SM83 code with M-cycle counts; record when each write happens."""
    def __init__(s, org): s.org, s.b, s.t, s.ev, s.lab, s.fix = org, bytearray(), 0, [], {}, []
    def pc(s): return s.org + len(s.b)
    def op(s, bs, cyc, ev=None, at=0):
        if ev: s.ev.append((s.t + at, ev))
        s.b += bytes(bs); s.t += cyc
    def label(s, n): s.lab[n] = s.pc()
    def nop(s): s.op([0x00], 1)
    def ld_a_r(s, r): s.op([0x78 + "bcdehl".index(r)], 1)
    def pop(s, rr): s.op([{"bc": 0xC1, "de": 0xD1, "hl": 0xE1}[rr]], 3)
    def ldh(s, n, what): s.op([0xE0, n], 3, what, 2)                  # write in the 3rd M-cycle
    def ld_hli_a(s): s.op([0x22], 2, "vram", 1)                         # write in the 2nd
    def ld_nn_a(s, nn): s.op([0xEA, nn & 0xFF, nn >> 8], 4, "mbc", 3)
    def ld_sp_hl(s): s.op([0xF9], 2)
    def ld_hd_le(s): s.op([0x62, 0x6B], 2)                              # ld h,d; ld l,e
    def jp(s, n): s.fix.append((len(s.b) + 1, n)); s.op([0xC3, 0, 0], 4)
    def delay(s, n):
        """exactly n M-cycles: ld a,N; .l: dec a; jr nz,.l = 4N + 1; then nops. Clobbers A."""
        assert n >= 0
        if n >= 5:
            N = min(255, (n - 1) // 4); s.op([0x3E, N, 0x3D, 0x20, 0xFD], 4 * N + 1); n -= 4 * N + 1
        for _ in range(n): s.nop()
    def to(s, t): s.delay(t - s.t)
    def link(s):
        for at, n in s.fix: s.b[at], s.b[at + 1] = s.lab[n] & 0xFF, s.lab[n] >> 8

def kernel(org):
    k = Asm(org); k.label("vb")
    # ---- VBlank block: lines 144..153, sound at 6 + 114 j ----
    k.pop("bc")                                  # c, b = sound of lines 144, 145 (previous frame's tail)
    k.ld_a_r("c"); k.ldh(0x24, "nr50")
    k.pop("de"); k.pop("hl")                     # e, d = next bank; hl = next frame address
    k.ld_a_r("e"); k.ld_nn_a(0x2000); k.ld_a_r("d"); k.ld_nn_a(0x3000)
    k.ld_sp_hl()                                 # now reading the new frame
    k.to(LINE * 1 + 3); k.ld_a_r("b"); k.ldh(0x24, "nr50")
    k.pop("bc"); k.pop("de")                     # sound of lines 146..149
    k.to(LINE * 2 + 3); k.ld_a_r("c"); k.ldh(0x24, "nr50")
    k.to(LINE * 3 + 3); k.ld_a_r("b"); k.ldh(0x24, "nr50")
    k.pop("bc")                                  # 150, 151
    k.to(LINE * 4 + 3); k.ld_a_r("e"); k.ldh(0x24, "nr50")
    k.to(LINE * 5 + 3); k.ld_a_r("d"); k.ldh(0x24, "nr50")
    k.pop("de")                                  # 152, 153
    k.to(LINE * 6 + 3); k.ld_a_r("c"); k.ldh(0x24, "nr50")
    k.to(LINE * 7 + 3); k.ld_a_r("b"); k.ldh(0x24, "nr50")
    k.to(LINE * 8 + 3); k.ld_a_r("e"); k.ldh(0x24, "nr50")
    k.to(LINE * 9 + 3); k.ld_a_r("d"); k.ldh(0x24, "nr50")
    k.pop("bc"); k.ld_a_r("c"); k.ldh(0x40, "lcdc"); k.ld_a_r("b"); k.ldh(0x47, "bgp")
    k.pop("de"); k.ld_a_r("e"); k.ldh(0x42, "scy"); k.ld_a_r("d"); k.ldh(0x43, "scx")
    k.pop("hl")                                  # where this frame's picture bytes go
    k.to(LINE * 10)
    # ---- 144 line blocks ----
    for line in range(VIS):
        t = k.t
        k.pop("bc"); k.ld_a_r("c"); k.ldh(0x24, "nr50")         # c = sound, b = picture byte 0
        k.pop("de")                                             # bytes 1, 2
        k.ld_a_r("b"); k.ld_hli_a(); k.ld_a_r("e"); k.ld_hli_a(); k.ld_a_r("d"); k.ld_hli_a()
        for _ in range(3):
            k.pop("bc"); k.ld_a_r("c"); k.ld_hli_a(); k.ld_a_r("b"); k.ld_hli_a()
        k.pop("bc"); k.ld_a_r("c"); k.ld_hli_a()               # byte 9; b = SCX of the next line
        k.ld_a_r("b"); k.ldh(0x43, "scx")
        if line < VIS - 1: k.to(t + LINE)
        else: k.to(t + LINE - 4); k.jp("vb")
    assert k.t == FRAME, k.t
    k.link(); check(k.ev)
    return k

def check(ev):
    """Every sound write 114 M-cycles after the last; video RAM only in the safe window."""
    s = [t for t, e in ev if e == "nr50"]
    assert len(s) == LINES and all(b - a == LINE for a, b in zip(s, s[1:])) and (s[0] + FRAME - s[-1]) == LINE, s[:12]
    worst = 99
    for t, e in ev:
        if e != "vram": continue
        lt = t - LINE * 10 + T0                  # time within the line its block starts in
        x = lt % LINE
        if x < SAFE[0]: x += LINE                # the part that runs into the next line's mode 2
        if "GBV_T0" in os.environ: continue
        assert SAFE[0] <= x <= SAFE[1] - 1, (t, x)
        worst = min(worst, x - SAFE[0], SAFE[1] - 1 - x)
    return worst

def init(org, maps_at):
    a = Asm(org)
    def ldh_n(reg, v): a.op([0x3E, v, 0xE0, reg], 5)
    a.op([0xF3, 0x31, 0xFE, 0xFF], 4)                                   # di; ld sp,$FFFE
    a.label("w"); a.op([0xF0, 0x44, 0xFE, 144, 0x20, 0xFA], 7)          # .w: ldh a,[LY]; cp 144; jr nz,.w
    ldh_n(0x40, 0x00)                                                   # LCD off (in VBlank)
    a.op([0x21, 0x00, 0x80, 0x01, 0x00, 0x18,                           # ld hl,$8000; ld bc,$1800
          0xAF, 0x22, 0x0B, 0x78, 0xB1, 0x20, 0xF9], 0)                 # .c: xor a; ld [hl+],a; dec bc; ld a,b; or c; jr nz
    a.op([0x11, maps_at & 0xFF, maps_at >> 8, 0x21, 0x00, 0x98, 0x01, 0x00, 0x08,   # ld de,maps; ld hl,$9800; ld bc,$800
          0x1A, 0x13, 0x22, 0x0B, 0x78, 0xB1, 0x20, 0xF8], 0)           # .m: ld a,[de]; inc de; ld [hl+],a; dec bc; ld a,b; or c; jr nz
    ldh_n(0x26, 0x80); ldh_n(0x24, 0x00); ldh_n(0x25, 0x44); ldh_n(0x1A, 0x00)   # sound on, wave channel to both sides
    a.op([0x21, 0x30, 0xFF, 0x06, 0x10, 0x3E, 0xFF, 0x22, 0x05, 0x20, 0xFC], 0)  # wave RAM = all 15: a constant
    ldh_n(0x1A, 0x80); ldh_n(0x1C, 0x20); ldh_n(0x1D, 0x00); ldh_n(0x1E, 0x87)
    ldh_n(0x47, BGP); ldh_n(0x42, SCY); ldh_n(0x43, 0)
    a.op([0x3E, 0x01, 0xEA, 0x00, 0x20, 0xAF, 0xEA, 0x00, 0x30], 0)     # bank 1
    ldh_n(0x40, LCDC[1])                                                # LCD on, buffer B (black)
    ldh_n(0xFF, 0x01); ldh_n(0x0F, 0x00)                                # IE = VBlank; IF = 0
    a.op([0xFB, 0x76, 0x00], 0)                                         # ei; halt -> $0040 -> sync
    a.label("hang"); a.op([0x18, 0xFE], 0)
    return a

def sync(org, vb):
    """Entered LATENCY M-cycles after line 144 starts; jump into the kernel at T0."""
    a = Asm(org); a.t = LATENCY
    a.op([0x31, 0x00, 0x40], 3)                                         # ld sp,$4000: the stream
    a.delay(T0 - LATENCY - 3 - 4)
    a.op([0xC3, vb & 0xFF, vb >> 8], 4)
    assert a.t == T0
    return a

def maps():
    m = bytearray([BORDER]) * 0x800
    for ty in range(PH // 8):
        for tx in range(PW // 8):
            j = ty * (PW // 8) + tx; o = (ROW0 + ty) * 32 + COL0 + tx
            m[o] = j                                                    # map A: tiles from $8000
            m[0x400 + o] = 192 + j if j < 64 else j - 64                # map B: $8C00.. in $8800 addressing
    return bytes(m)

def tiles(pic):
    """128 x 88 levels 0..3 (row-major bytes) -> 176 tiles, 2 bits a pixel."""
    out = bytearray()
    for ty in range(PH // 8):
        for tx in range(PW // 8):
            for r in range(8):
                row = pic[(ty * 8 + r) * PW + tx * 8:(ty * 8 + r) * PW + tx * 8 + 8]
                lo = hi = 0
                for x, q in enumerate(row): lo |= (q & 1) << (7 - x); hi |= (q >> 1) << (7 - x)
                out += bytes([lo, hi])
    return bytes(out)

def stream(levels, scx, pics):
    """levels: sound 0..7 per line; scx: per line; pics: list of 128x88 level pictures."""
    n = len(levels)
    K = max(2, (n + LINES - 1) // LINES)                                # kernel frames
    lv = lambda i: levels[i % n] * 0x11
    sx = lambda i: scx[i % n] & 0xFF
    tl = [tiles(p) + bytes(FBYTES * 2 - PBYTES) for p in pics]
    chunks = []
    for k in range(K):
        base = k * LINES                                                # sample index of line 144 of this kernel frame
        p, half = k // 2, k % 2
        shown = (k - 2) // 2
        head = bytes(lv(base + i) for i in range(2, 10))
        head += bytes([LCDC[shown % 2] if shown >= 0 else LCDC[1], BGP, SCY, sx(base + 10)])
        dest = BUF[p % 2] + FBYTES * half; head += bytes([dest & 0xFF, dest >> 8])
        vid = tl[p % len(tl)][FBYTES * half:FBYTES * (half + 1)]
        body = bytearray()
        for L in range(VIS):
            i = base + 10 + L
            body += bytes([lv(i)]) + vid[L * PER_LINE:(L + 1) * PER_LINE] + bytes([sx(i + 1)])
        chunks.append([head, bytes(body)])
    # lay out in banks: a frame never straddles two banks
    place, bank, off = [], 1, TAIL                                      # bank 1 starts with the first tail
    for c in chunks:
        if off + CHUNK > BANK: bank, off = bank + 1, 0
        place.append((bank, off)); off += CHUNK
    nb = bank + 1
    if nb > 512: sys.exit("mkgbv: too long for an 8 MiB cartridge")
    data = bytearray(b"\xff" * ((nb - 1) * BANK))
    def tail(k_next, base_next):
        b, o = place[k_next % K]
        return bytes([lv(base_next), lv(base_next + 1), b & 0xFF, b >> 8, (0x4000 + o) & 0xFF, (0x4000 + o) >> 8])
    data[0:TAIL] = tail(0, 0)
    for k, (h, body) in enumerate(chunks):
        b, o = place[k]; at = (b - 1) * BANK + o
        blob = h + body + tail(k + 1, ((k + 1) % K) * LINES)
        assert len(blob) == CHUNK; data[at:at + CHUNK] = blob
    return bytes(data), nb, K

def build(levels, scx, pics, title="FAUST"):
    pad = DELAY * LINES                                                 # sound waits for the picture
    levels = bytes([levels[0]]) * pad + bytes(levels); scx = bytes([scx[0]]) * pad + bytes(scx)
    data, nb, K = stream(levels, scx, pics)
    size = 2
    while size < nb: size *= 2
    rom = bytearray(b"\xff" * (size * BANK))
    rom[0x40:0x43] = bytes([0xC3, 0x00, 0x00])                          # VBlank vector -> sync (patched below)
    rom[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01]); rom[0x104:0x134] = LOGO
    t = title.upper().encode("ascii", "replace")[:15]; rom[0x134:0x144] = t + bytes(16 - len(t))
    rom[0x143], rom[0x147], rom[0x149], rom[0x14A] = 0x00, 0x19, 0x00, 0x01
    rom[0x148] = (size * BANK // 0x8000).bit_length() - 1
    MAPS = 0x1000; KER = 0x2000
    ker = kernel(KER); rom[KER:KER + len(ker.b)] = ker.b
    syn = sync(0x0F00, KER); rom[0x0F00:0x0F00 + len(syn.b)] = syn.b
    rom[0x41], rom[0x42] = 0x00, 0x0F
    ini = init(0x150, MAPS); assert 0x150 + len(ini.b) < 0x0F00; rom[0x150:0x150 + len(ini.b)] = ini.b
    rom[MAPS:MAPS + 0x800] = maps()
    assert KER + len(ker.b) <= BANK, len(ker.b)
    rom[BANK:BANK + len(data)] = data
    x = 0
    for i in range(0x134, 0x14D): x = (x - rom[i] - 1) & 0xFF
    rom[0x14D] = x
    s = (sum(rom) - rom[0x14E] - rom[0x14F]) & 0xFFFF; rom[0x14E], rom[0x14F] = s >> 8, s & 0xFF
    return bytes(rom), nb, K, len(ker.b), check(ker.ev)

if __name__ == "__main__":
    lev = open(sys.argv[1], "rb").read(); scx = open(sys.argv[2], "rb").read()
    raw = open(sys.argv[3], "rb").read(); P = PW * PH
    pics = [raw[i:i + P] for i in range(0, len(raw) - P + 1, P)]
    rom, nb, K, kl, margin = build(lev, scx, pics, sys.argv[5] if len(sys.argv) > 5 else "FAUST")
    open(sys.argv[4], "wb").write(rom)
    print(f"mkgbv: {sys.argv[4]}  {len(rom) // 1024} KiB, MBC5, {nb - 1} banks, {K} frames ({K / FPS:.1f} s), "
          f"{len(pics)} pictures, kernel {kl} bytes, video-RAM writes >= {margin} M-cycles inside the safe window")
