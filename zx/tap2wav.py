#!/usr/bin/env python3
"""Turn a .tap into the audio a real ZX Spectrum can LOAD from its EAR socket.

The ROM loader is timing-based, so these numbers are the specification, not
preferences (all in Z80 T-states at 3.5 MHz):

    pilot pulse      2168      leader tone
    pilot length     8063 pulses for a header block, 3223 for a data block
    sync 1            667
    sync 2            735
    bit 0             855 x2
    bit 1            1710 x2

Each byte goes out MSB first, and the signal is a square wave that flips level
at every pulse boundary - the loader measures edge-to-edge times, so only the
timings matter, not the polarity.

Play the result into the Spectrum's EAR socket at a healthy level and type
LOAD "".
"""
import struct, sys, wave

T = 3_500_000
SR = 44100

PILOT, PILOT_HDR, PILOT_DAT = 2168, 8063, 3223
SYNC1, SYNC2 = 667, 735
BIT0, BIT1 = 855, 1710
AMP = 26000


class Tape:
    def __init__(self, sr=SR):
        self.sr, self.buf, self.level, self.frac = sr, bytearray(), 1, 0.0

    def pulse(self, t_states):
        n = t_states * self.sr / T + self.frac
        k = int(n)
        self.frac = n - k
        v = struct.pack("<h", AMP if self.level > 0 else -AMP)
        self.buf += v * k
        self.level = -self.level

    def silence(self, ms):
        self.buf += b"\x00\x00" * int(self.sr * ms / 1000.0)

    def byte(self, b):
        for i in range(7, -1, -1):
            w = BIT1 if (b >> i) & 1 else BIT0
            self.pulse(w); self.pulse(w)

    def block(self, data):
        n = PILOT_HDR if (data and data[0] == 0x00) else PILOT_DAT
        for _ in range(n):
            self.pulse(PILOT)
        self.pulse(SYNC1); self.pulse(SYNC2)
        for b in data:
            self.byte(b)
        self.silence(1000)


def convert(tap_path, wav_path):
    raw = open(tap_path, "rb").read()
    t = Tape()
    t.silence(500)
    i, blocks = 0, 0
    while i + 2 <= len(raw):
        ln = struct.unpack("<H", raw[i:i + 2])[0]
        i += 2
        t.block(raw[i:i + ln])
        i += ln
        blocks += 1
    w = wave.open(wav_path, "wb")
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes(bytes(t.buf))
    w.close()
    return blocks, len(t.buf) / 2 / SR


if __name__ == "__main__":
    nb, secs = convert(sys.argv[1], sys.argv[2])
    print("tap2wav: %d blocks -> %s  (%.1f s of tape audio at %d Hz)"
          % (nb, sys.argv[2], secs, SR))
