#!/usr/bin/env python3
"""Turn a rendered mono WAV into ZX Spectrum 1-bit PWM sample data.

THE CONSTRAINT. The Spectrum has one bit of audio: bit 4 of port 0xFE drives
the speaker. Everything it has ever played is that bit being toggled by the
CPU, and the CPU is a 3.5 MHz Z80 with nothing else to help.

THE METHOD. Pulse width modulation. Each sample is one fixed-length period in
which the speaker is held high for W ticks and low for the rest. The ear
integrates that into an amplitude. The period has to be constant or the pitch
wobbles, so the player spends exactly the same time per sample regardless of W.

THE ARITHMETIC that sets everything else:

    Z80          3500000 T-states/sec
    DJNZ loop    13 T-states per iteration
    period       LEVELS * 13 T-states  (high phase + low phase together)
    sample rate  3500000 / (LEVELS * 13 + overhead)

so LEVELS and the sample rate trade directly against each other. At 32 levels
(5 bits) the rate lands near 8 kHz, which is the classic Spectrum compromise
and what this uses.
"""
import struct, sys, wave

T_PER_SEC   = 3_500_000
LEVELS      = 32          # 5 bits of amplitude
# Counted instruction by instruction from the player in zx/mktap.py, not
# estimated: 95 T of fixed work plus 13*(LEVELS-2)+16 of delay, and the delay
# total is independent of the sample value by construction.
T_FIXED     = 95
T_TOTAL     = T_FIXED + 13 * (LEVELS - 2) + 16


def sample_rate(levels=LEVELS):
    return T_PER_SEC / (T_FIXED + 13 * (levels - 2) + 16)


def encode(path, levels=LEVELS):
    w = wave.open(path)
    assert w.getsampwidth() == 2, "expected 16-bit"
    n, ch = w.getnframes(), w.getnchannels()
    raw = struct.unpack("<%dh" % (n * ch), w.readframes(n))
    mono = raw[0::ch]
    out = bytearray()
    # Error diffusion: 5 bits is coarse, and pushing the quantisation error
    # into the next sample moves the noise up out of the way instead of
    # leaving it as a buzz sitting on the signal.
    err = 0.0
    for s in mono:
        v = s / 32768.0                      # -1..1
        x = (v + 1.0) * 0.5 * (levels - 1)   # 0..levels-1
        x += err
        q = int(round(x))
        if q < 1: q = 1                      # 0 and LEVELS-1 would stall a DJNZ
        if q > levels - 1: q = levels - 1
        err = x - q
        out.append(q)
    return bytes(out)


if __name__ == "__main__":
    data = encode(sys.argv[1])
    open(sys.argv[2], "wb").write(data)
    print("encode: %d samples, %.1f Hz, %d bytes (%.2f s)"
          % (len(data), sample_rate(), len(data), len(data) / sample_rate()))
