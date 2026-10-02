#!/usr/bin/env python3
"""mkgb.py PCM.bin OUT.gb [TITLE] - a Game Boy ROM that plays master-volume PCM.

No assembler needed: the player is a few dozen bytes, hand-assembled below with
every instruction's M-cycles. Cartridge: MBC5 (bank 0 = code, banks 1.. = samples,
16 KiB = 2 s each), loops forever.

THE TIMING, counted (M-cycles; the CPU runs 1,048,576 of them a second):
    loop:  ld a,[hl+]      2
           ldh [NR50],a    3     <- the sample goes out here
           ld b,29         2
    .d:    dec b           1  \\  29 passes: 28 x (1 + 3) + (1 + 2) = 115
           jr nz,.d        3/2 /
           ld a,h          1
           cp $80          2
           jr nz,loop      3
    total  2 + 3 + 2 + 115 + 1 + 2 + 3 = 128  ->  8192 samples a second, whatever the value
At the end of a bank (h = $80) the bank switch adds 17 M-cycles once: that one
sample is 145 cycles long (simulated: 17 extra), once every 2 seconds.
"""
import sys
LOGO = bytes.fromhex("CEED6666CC0D000B03730083000C000D0008111F8889000EDCCC6EE6DDDDD999"
                     "BBBB67636E0EECCCDDDC999FBBB9333E")
BANK = 0x4000

def build(pcm, title="FAUST"):
    nbanks = 1 + (len(pcm) + BANK - 1) // BANK
    if nbanks - 1 > 254: sys.exit("mkgb: more than 254 sample banks (about 8 minutes)")
    size = 2
    while size < nbanks: size *= 2                        # ROM sizes are powers of two
    rom = bytearray(b"\xff" * (size * BANK))
    rom[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])    # nop; jp $0150
    rom[0x104:0x134] = LOGO                               # the boot ROM checks this
    t = title.upper().encode("ascii", "replace")[:15]
    rom[0x134:0x144] = t + bytes(16 - len(t))
    rom[0x143] = 0x00                                     # DMG
    rom[0x147] = 0x19                                     # MBC5
    rom[0x148] = (size * BANK // 0x8000).bit_length() - 1 # 32 KiB << n
    rom[0x149] = 0x00                                     # no cartridge RAM
    rom[0x14A] = 0x01                                     # non-Japanese
    last = nbanks - 1
    code = bytes([
        0xF3,                         # di
        0x31, 0xFE, 0xFF,             # ld sp,$FFFE
        0x3E, 0x80, 0xE0, 0x26,       # NR52 = $80: sound on
        0x3E, 0x00, 0xE0, 0x24,       # NR50 = 0
        0x3E, 0x44, 0xE0, 0x25,       # NR51 = $44: wave channel to both sides
        0x3E, 0x00, 0xE0, 0x1A,       # NR30 = 0: wave DAC off while wave RAM is written
        0x21, 0x30, 0xFF,             # ld hl,$FF30
        0x06, 0x10,                   # ld b,16
        0x3E, 0xFF,                   # ld a,$FF
        0x22, 0x05, 0x20, 0xFC,       # .w: ld [hl+],a; dec b; jr nz,.w   (wave = all 15: a constant)
        0x3E, 0x80, 0xE0, 0x1A,       # NR30 = $80: DAC on
        0x3E, 0x20, 0xE0, 0x1C,       # NR32 = $20: full level
        0x3E, 0x00, 0xE0, 0x1D,       # NR33 = 0
        0x3E, 0x87, 0xE0, 0x1E,       # NR34 = trigger
        0x0E, 0x01,                   # ld c,1          (current sample bank)
        0x79, 0xEA, 0x00, 0x20,       # ld a,c; ld [$2000],a
        0xAF, 0xEA, 0x00, 0x30,       # xor a; ld [$3000],a
        0x21, 0x00, 0x40,             # ld hl,$4000
    ])
    loop = 0x150 + len(code)
    body = bytes([
        0x2A,                         # loop: ld a,[hl+]
        0xE0, 0x24,                   # ldh [NR50],a
        0x06, 29,                     # ld b,29
        0x05, 0x20, 0xFD,             # .d: dec b; jr nz,.d
        0x7C,                         # ld a,h
        0xFE, 0x80,                   # cp $80
    ])
    jr_back = (loop - (loop + len(body) + 2)) & 0xFF
    body += bytes([0x20, jr_back])    # jr nz,loop
    tail = bytes([
        0x0C,                         # inc c
        0x79, 0xFE, last + 1,         # ld a,c; cp LAST+1
        0x20, 0x02,                   # jr nz,.ok
        0x0E, 0x01,                   # ld c,1          (loop back to the first bank)
        0x79, 0xEA, 0x00, 0x20,       # .ok: ld a,c; ld [$2000],a
        0x21, 0x00, 0x40,             # ld hl,$4000
    ])
    here = loop + len(body) + len(tail)
    tail += bytes([0x18, (loop - (here + 2)) & 0xFF])     # jr loop
    prog = code + body + tail
    rom[0x150:0x150 + len(prog)] = prog
    # the samples, from bank 1; the rest of the last bank holds the middle level (silence)
    data = pcm + bytes([0x33]) * ((last * BANK) - len(pcm))
    rom[BANK:BANK + len(data)] = data
    x = 0
    for i in range(0x134, 0x14D): x = (x - rom[i] - 1) & 0xFF
    rom[0x14D] = x                                        # header checksum (the boot ROM checks it)
    s = (sum(rom) - rom[0x14E] - rom[0x14F]) & 0xFFFF
    rom[0x14E], rom[0x14F] = s >> 8, s & 0xFF             # global checksum
    return bytes(rom), loop, last

if __name__ == "__main__":
    pcm = open(sys.argv[1], "rb").read()
    rom, loop, last = build(pcm, sys.argv[3] if len(sys.argv) > 3 else "FAUST")
    open(sys.argv[2], "wb").write(rom)
    print(f"mkgb: {sys.argv[2]}  {len(rom) // 1024} KiB, MBC5, {last} sample banks, player loop at ${loop:04X}")
