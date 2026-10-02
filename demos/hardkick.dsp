// HARDKICK - pitch-enveloped kick with a distorted rumble tail, 4-to-the-floor
// at 180. The tail is the kick itself fed through more drive and a lowpass,
// which is how the rumble in this genre is actually made.
import("stdfaust.lib");

bpm    = hslider("bpm",     180, 140, 220, 1);
basef  = hslider("basef",    47, 30, 80, 1);
punch  = hslider("punch",  1400, 200, 4000, 10);
ptime  = hslider("ptime",  0.03, 0.005, 0.12, 0.001);
body   = hslider("body",   0.32, 0.08, 0.9, 0.01);
dist   = hslider("dist",     14, 1, 40, 0.5);
rumble = hslider("rumble", 0.55, 0, 1, 0.01);
gain   = hslider("gain",   0.55, 0, 1, 0.01);

stepn = int(60.0 * ma.SR / bpm);
t     = ba.time;
trig  = (t % stepn) < 1;

dcoef(s) = pow(0.001, 1.0 / max(1.0, s * ma.SR));
ad(c)    = _ : (max ~ *(c));

penv = trig : ad(dcoef(ptime));
aenv = trig : ad(dcoef(body));

f    = basef + penv * punch;
core = os.osc(f) * aenv;

// the click that makes it cut through a mix
click = trig : ad(dcoef(0.004)) : *(no.noise) : fi.highpass(1, 1800) : *(0.6);

driven = (core + click) : *(dist) : ma.tanh;
tail   = driven : fi.lowpass(2, 220) : *(dist * 0.8) : ma.tanh : *(rumble);

process = (driven * 0.7 + tail) : fi.dcblocker : *(gain) <: _, _;
