#!/usr/bin/env python3
"""Reproduce what the Spectrum's speaker actually does with a PWM stream.

The point is to check the encoding WITHOUT a Spectrum and without an emulator.
The player holds the speaker high for W T-states and low for LEVELS-W, at
3.5 MHz. That is a square wave at the sample rate whose duty cycle carries the
signal; a speaker and an ear both integrate it. So: expand the samples back
out to a 3.5 MHz two-level signal, lowpass it the way a speaker would, and
compare the result against the audio that went in.
"""
import math, struct, sys, wave

LEVELS = 32
T_PER_SEC = 3_500_000


def simulate(pwm, out_wav, out_sr=22050):
    """Integrate each sample's duty cycle - the mean of the two-level signal
    over one period IS W/LEVELS, so the reconstruction is exact without
    building a 3.5 MHz array."""
    n = len(pwm)
    sr_in = T_PER_SEC / (95 + 13 * (LEVELS - 2) + 16)
    vals = [(w / float(LEVELS)) * 2.0 - 1.0 for w in pwm]

    # one-pole lowpass at 3.5 kHz, standing in for the speaker and the ear
    a = math.exp(-2.0 * math.pi * 3500.0 / sr_in)
    y, sm = 0.0, []
    for v in vals:
        y = a * y + (1 - a) * v
        sm.append(y)

    ratio = sr_in / out_sr
    res = []
    for i in range(int(n / ratio)):
        res.append(sm[int(i * ratio)])
    w = wave.open(out_wav, "wb")
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(out_sr)
    w.writeframes(b"".join(struct.pack("<h", max(-32000, min(32000, int(v * 32000))))
                           for v in res))
    w.close()
    return sr_in, vals


def compare(src_wav, pwm):
    """Correlate the reconstruction against the source, sample for sample."""
    w = wave.open(src_wav); n = w.getnframes(); ch = w.getnchannels()
    raw = struct.unpack("<%dh" % (n * ch), w.readframes(n))
    src = [x / 32768.0 for x in raw[0::ch]]
    rec = [(v / float(LEVELS)) * 2.0 - 1.0 for v in pwm]
    m = min(len(src), len(rec))
    src, rec = src[:m], rec[:m]
    ms = sum(src) / m
    mr = sum(rec) / m
    cs = sum((x - ms) ** 2 for x in src)
    cr = sum((x - mr) ** 2 for x in rec)
    cx = sum((x - ms) * (y - mr) for x, y in zip(src, rec))
    corr = cx / math.sqrt(cs * cr) if cs > 0 and cr > 0 else 0.0
    err = [(x - ms) - (y - mr) for x, y in zip(src, rec)]
    re_ = math.sqrt(sum(e * e for e in err) / m)
    rs = math.sqrt(cs / m)
    # Guard BOTH terms. re_ == 0 means a perfect match; rs == 0 means the
    # SOURCE was silent, so there is no signal to have a ratio to and log10(0)
    # raises rather than returning -inf. A button-gated DSP rendered with no
    # trigger is silent, which is how this was found.
    if re_ <= 0.0:
        snr = float("inf")
    elif rs <= 0.0:
        snr = float("-inf")
    else:
        snr = 20 * math.log10(rs / re_)
    return corr, snr, m


if __name__ == "__main__":
    pwm = open(sys.argv[1], "rb").read()
    sr_in, _ = simulate(pwm, sys.argv[3])
    corr, snr, m = compare(sys.argv[2], pwm)
    print("simulate: %d samples at %.1f Hz" % (len(pwm), sr_in))
    print("          correlation with source  %.4f" % corr)
    print("          SNR of the 5-bit encode  %.1f dB" % snr)
    print("          wrote %s" % sys.argv[3])
