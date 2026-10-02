// SCHRANZ - a distorted, filtered loop with an offbeat clap, 150 BPM.
// Everything is synthesised: the "loop" is noise shaped by a comb and a
// moving bandpass, which is what makes it sound like a chopped sample.
import("stdfaust.lib");

bpm   = hslider("bpm",   150, 130, 190, 1);
grind = hslider("grind",  12, 1, 40, 0.5);
sweep = hslider("sweep", 900, 200, 5000, 10);
combf = hslider("combf", 180, 40, 600, 1);
gain  = hslider("gain",  0.5, 0, 1, 0.01);

stepn = int(60.0 * ma.SR / (bpm * 4.0));
t     = ba.time;
trig  = (t % stepn) < 1;
step  = int(t / stepn) % 16;

dcoef(s) = pow(0.001, 1.0 / max(1.0, s * ma.SR));
ad(c)    = _ : (max ~ *(c));

// kick on every 4th 16th
khit = trig * ((step % 4) == 0);
kenv = khit : ad(dcoef(0.26));
kpit = khit : ad(dcoef(0.028));
kick = os.osc(45 + kpit * 1100) * kenv : *(grind) : ma.tanh;

// clap on the offbeat
chit = trig * ((step % 8) == 4);
cenv = chit : ad(dcoef(0.09));
clap = no.noise * cenv : fi.bandpass(2, 1100, 3000) : *(0.5);

// the "loop": comb-filtered noise through a swept bandpass
lenv = trig : ad(dcoef(0.07));
loop = no.noise * lenv
     : (+ : de.fdelay(2048, ma.SR/combf) : *(0.82)) ~ _
     : fi.bandpass(2, 300 + lenv*sweep, 900 + lenv*sweep*2)
     : *(grind * 0.4) : ma.tanh : *(0.45);

process = (kick * 0.8 + clap + loop) : fi.dcblocker : *(gain) <: _, _;
