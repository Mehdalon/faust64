// FRENCHCORE - 205 BPM kick with a pitched, screaming tail and an acid lead
// riding on top. The kick's tail is tuned, so it plays a note, which is the
// whole trick of the genre.
import("stdfaust.lib");

bpm   = hslider("bpm",   205, 180, 250, 1);
kbase = hslider("kbase",  49, 30, 80, 1);
ktune = hslider("ktune",  73, 40, 160, 1);
punch = hslider("punch", 2600, 400, 6000, 10);
dist  = hslider("dist",    34, 1, 90, 0.5);
lead  = hslider("lead",  0.35, 0, 1, 0.01);
gain  = hslider("gain",  0.52, 0, 1, 0.01);

stepn = int(60.0 * ma.SR / bpm);
t     = ba.time;
ktrig = (t % stepn) < 1;
s16   = int(60.0 * ma.SR / (bpm * 4.0));
ltrig = (t % s16) < 1;
lstep = int(t / s16) % 16;

dcoef(s) = pow(0.001, 1.0 / max(1.0, s * ma.SR));
ad(c)    = _ : (max ~ *(c));

// kick: fast pitch drop into a TUNED, distorted tail
penv  = ktrig : ad(dcoef(0.018));
aenv  = ktrig : ad(dcoef(0.34));
tenv  = ktrig : ad(dcoef(0.30));
head  = os.osc(kbase + penv * punch) * aenv;
tailo = os.osc(ktune) * tenv;
kick  = (head + tailo * 0.9) : *(dist) : ma.tanh
      : fi.lowpass(2, 900) : *(dist * 0.7) : ma.tanh;

// acid lead over the top
pat   = waveform{12, 0, 15, 0, 12, 19, 0, 15, 12, 0, 22, 19, 15, 0, 12, 0};
gpat  = waveform{ 1, 0,  1, 0,  1,  1, 0,  1,  1, 0,  1,  1,  1, 0,  1, 0};
n     = pat,  lstep : rdtable;
g     = gpat, lstep : rdtable;
lhit  = ltrig * g;
lenv  = lhit : ad(dcoef(0.10));
lf    = 110.0 * pow(2.0, n / 12.0);
ldv   = os.sawtooth(lf) * lenv
      : fi.resonlp(min(0.45*ma.SR, 400 + lenv * 4200), 12, 1)
      : *(4) : ma.tanh : *(lead);

process = (kick + ldv) : fi.highpass(1, 30) : fi.dcblocker : *(1.2) : ma.tanh
        : *(gain) <: _, _;
