// FM - two-operator FM with a moving index, playing a bell sequence.
// The classic cheap-and-huge sound; this one is also cheap on the VR4300.
import("stdfaust.lib");

bpm   = hslider("bpm",   124, 60, 200, 1);
ratio = hslider("ratio", 2.01, 0.5, 8, 0.01);
index = hslider("index",  6.5, 0, 20, 0.1);
idec  = hslider("idec",  0.22, 0.02, 1.5, 0.01);
adec  = hslider("adec",  0.75, 0.05, 3, 0.01);
gain  = hslider("gain",  0.45, 0, 1, 0.01);

stepn = int(60.0 * ma.SR / (bpm * 2.0));
t     = ba.time;
trig  = (t % stepn) < 1;
step  = int(t / stepn) % 8;

dcoef(s) = pow(0.001, 1.0 / max(1.0, s * ma.SR));
ad(c)    = _ : (max ~ *(c));

pat = waveform{0, 12, 7, 15, 3, 10, 19, 7};
n   = pat, step : rdtable;
f   = 220.0 * pow(2.0, n / 12.0);

ienv = trig : ad(dcoef(idec));
aenv = trig : ad(dcoef(adec));

mod  = os.osc(f * ratio) * index * ienv * f;
car  = os.osc(f + mod) * aenv;

process = car : fi.dcblocker : *(gain) <: _, _;
