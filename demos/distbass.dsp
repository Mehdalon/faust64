// DISTBASS - a neuro-style reese: three detuned saws, driven hard, then torn
// apart by a moving notch. The movement is what makes it growl rather than buzz.
import("stdfaust.lib");

freq   = hslider("freq",    41, 20, 200, 0.5);
det    = hslider("detune", 0.55, 0, 3, 0.01);
drive  = hslider("drive",    18, 1, 60, 0.5);
rate   = hslider("rate",    2.8, 0.05, 12, 0.01);
notchf = hslider("notchf",  520, 100, 4000, 10);
depth  = hslider("depth",  0.85, 0, 1, 0.01);
tone   = hslider("tone",   2600, 300, 8000, 10);
gain   = hslider("gain",   0.45, 0, 1, 0.01);

saws = os.sawtooth(freq)
     + os.sawtooth(freq * (1 + det/100))
     + os.sawtooth(freq * (1 - det/100));

// hard drive first, THEN the notch: distorting after the filter just buzzes
driven = saws / 3 : *(drive) : ma.tanh;

lfo  = os.osc(rate) * 0.5 + 0.5;
nf   = notchf * pow(2.0, (lfo - 0.5) * 3.0 * depth);
carved = driven : fi.notchw(nf * 0.35, nf) : fi.notchw(nf * 0.5, nf * 2.02);

process = carved : fi.lowpass(2, tone) : *(1.6) : ma.tanh
        : fi.dcblocker : *(gain) <: _, _;
