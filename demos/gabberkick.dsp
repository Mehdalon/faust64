// GABBERKICK - overdriven kick, 190 BPM. The tail is the kick distorted again
// and lowpassed, then fed back in slightly, which is where the "hoover-ish"
// grind in a gabber kick actually comes from.
import("stdfaust.lib");

bpm    = hslider("bpm",    190, 150, 250, 1);
basef  = hslider("basef",   52, 30, 90, 1);
punch  = hslider("punch",  2200, 200, 5000, 10);
ptime  = hslider("ptime", 0.022, 0.004, 0.1, 0.001);
body   = hslider("body",   0.40, 0.08, 1.2, 0.01);
dist   = hslider("dist",     28, 1, 80, 0.5);
tail   = hslider("tail",   0.75, 0, 1, 0.01);
tonef  = hslider("tonef",   260, 80, 1200, 5);
gain   = hslider("gain",   0.55, 0, 1, 0.01);

stepn = int(60.0 * ma.SR / bpm);
t     = ba.time;
trig  = (t % stepn) < 1;

dcoef(s) = pow(0.001, 1.0 / max(1.0, s * ma.SR));
ad(c)    = _ : (max ~ *(c));

penv = trig : ad(dcoef(ptime));
aenv = trig : ad(dcoef(body));

core   = os.osc(basef + penv * punch) * aenv;
click  = no.noise * (trig : ad(dcoef(0.005))) : fi.highpass(2, 2600) : *(0.5);

stage1 = (core + click) : *(dist) : ma.tanh;
stage2 = stage1 : fi.lowpass(2, tonef) : *(dist * 1.2) : ma.tanh : *(tail);

process = (stage1 * 0.6 + stage2) : fi.highpass(1, 32) : fi.dcblocker
        : *(1.3) : ma.tanh : *(gain) <: _, _;
