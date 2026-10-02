#!/usr/bin/env python3
"""Build a ZX Spectrum .tap from PWM sample data: Z80 player + BASIC loader.

THE PLAYER, and why its timing is counted rather than estimated.

Pulse width modulation only works if every sample occupies the SAME number of
T-states; if the period varies with the sample value, the playback pitch
wobbles with the waveform. So the loop is arranged as two DJNZ delays whose
lengths always sum to the same total:

    high phase   W ticks
    low  phase   LEVELS - W ticks

Counted from the Z80 timings, per sample:

    ld a,(hl)      7      xor a           4
    inc hl         6      out (0xfe),a   11
    ld c,a         4      ld a,LEVELS     7
    ld b,a         4      sub c           4
    ld a,16        7      ld b,a          4
    out (0xfe),a  11      djnz (low)     13*(LEVELS-W-1)+8
    djnz (high)   13*(W-1)+8             dec de          6
                                          ld a,d          4
                                          or e            4
                                          jr nz,loop     12

    fixed  = 95 T
    delays = 13*(W-1)+8 + 13*(LEVELS-W-1)+8 = 13*(LEVELS-2)+16 = 406 T
    total  = 501 T, independent of W

    3500000 / 501 = 6986.0 Hz

W is kept in 1..LEVELS-1 by the encoder: W=0 or W=LEVELS would give DJNZ a
count of zero, which on a Z80 means 256 iterations, not none.
"""
import struct, sys

# 0x6000, not 0x8000: the Spectrum's RAM runs 0x4000-0xFFFF, with the screen
# at 0x4000-0x5AFF and BASIC just above it. Loading at 0x6000 leaves BASIC its
# ~800 bytes and gives the samples 40959 instead of 32767 - about 5.8 seconds
# instead of 4.6.
ORG    = 0x6000
LEVELS = 32
T_TOTAL = 501
SR = 3_500_000 / T_TOTAL


def player(data_addr, length, loop_forever=False):
    """Hand-assembled; there is no Z80 assembler on this machine and the
    program is short enough that adding one would be the bigger risk."""
    c = bytearray()
    c += b"\xF3"                                   # di
    c += b"\x21" + struct.pack("<H", data_addr)    # ld hl,data
    c += b"\x11" + struct.pack("<H", length)       # ld de,length
    loop = len(c)
    c += b"\x7E"                                   # ld a,(hl)
    c += b"\x23"                                   # inc hl
    c += b"\x4F"                                   # ld c,a
    c += b"\x47"                                   # ld b,a
    c += b"\x3E\x10"                               # ld a,16   (bit4 = speaker)
    c += b"\xD3\xFE"                               # out (0xfe),a
    c += b"\x10\xFE"                               # djnz $     high phase
    c += b"\xAF"                                   # xor a
    c += b"\xD3\xFE"                               # out (0xfe),a
    c += b"\x3E" + bytes([LEVELS])                 # ld a,LEVELS
    c += b"\x91"                                   # sub c
    c += b"\x47"                                   # ld b,a
    c += b"\x10\xFE"                               # djnz $     low phase
    c += b"\x1B"                                   # dec de
    c += b"\x7A"                                   # ld a,d
    c += b"\xB3"                                   # or e
    off = loop - (len(c) + 2)                      # jr is relative to the NEXT instruction
    c += b"\x20" + struct.pack("<b", off)          # jr nz,loop
    if loop_forever:
        # Start over. BREAK still works because the ROM's keyboard interrupt is
        # not what we disabled - but there is no exit, which is the point of a
        # demo: it plays until the machine is reset.
        c += b"\xC3" + struct.pack("<H", ORG + 1)  # jp start+1 (past the DI)
    else:
        c += b"\xFB"                               # ei
        c += b"\xC9"                               # ret
    return bytes(c)


def num(n):
    """A number in Spectrum BASIC is its digits followed by 0x0E and the
    5-byte binary form. Small integers use 00 sign lo hi 00."""
    return str(n).encode() + b"\x0E\x00\x00" + struct.pack("<H", n) + b"\x00"


def basic_loader(start):
    """10 CLEAR 32767: LOAD "" CODE : RANDOMIZE USR <start>"""
    s  = b"\xFD" + num(start - 1)      # CLEAR
    s += b"\x3A\xEF\x22\x22\xAF"       # : LOAD "" CODE
    s += b"\x3A\xF9\xC0" + num(start)  # : RANDOMIZE USR start
    s += b"\x0D"
    return struct.pack(">H", 10) + struct.pack("<H", len(s)) + s


def block(payload, flag):
    body = bytes([flag]) + payload
    chk = 0
    for b in body:
        chk ^= b
    body += bytes([chk])
    return struct.pack("<H", len(body)) + body


def header(ftype, name, length, p1, p2):
    return block(bytes([ftype]) + name.encode().ljust(10)[:10]
                 + struct.pack("<HHH", length, p1, p2), 0x00)


def build(pwm, out, name="faust", loop_forever=False):
    code = player(ORG + 0, 0, loop_forever)        # sized first
    data_addr = ORG + len(code)
    code = player(data_addr, len(pwm), loop_forever)
    assert data_addr == ORG + len(code), "player size changed between passes"
    blob = code + pwm

    bas = basic_loader(ORG)
    tap  = header(0, name, len(bas), 10, len(bas))
    tap += block(bas, 0xFF)
    tap += header(3, name, len(blob), ORG, 32768)
    tap += block(blob, 0xFF)
    open(out, "wb").write(tap)
    return len(code), len(blob), len(tap)


if __name__ == "__main__":
    pwm = open(sys.argv[1], "rb").read()
    loop = "--loop" in sys.argv
    nc, nb, nt = build(pwm, sys.argv[2], loop_forever=loop)
    print("mktap: player %d bytes, payload %d bytes, tap %d bytes" % (nc, nb, nt))
    print("       %d samples at %.1f Hz = %.2f s" % (len(pwm), SR, len(pwm) / SR))
    print("       loads to 0x%04X, data at 0x%04X, top used 0x%04X"
          % (ORG, ORG + nc, ORG + nb))
