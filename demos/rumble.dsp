// RUMBLE - hardtekk/uptempo rumble bass: a kick whose tail is fed back through
// a lowpass and a distortion, so the tail never quite stops before the next hit.
import("stdfaust.lib");

bpm   = hslider("bpm",   160, 130, 200, 1);
basef = hslider("basef",  44, 28, 70, 1);
fb    = hslider("fb",    0.7, 0, 0.95, 0.01);
tonef = hslider("tonef", 190, 60, 900, 5);
dist  = hslider("dist",   22, 1, 70, 0.5);
gain  = hslider("gain",  0.5, 0, 1, 0.01);

stepn = int(60.0 * ma.SR / bpm);
t     = ba.time;
trig  = (t % stepn) < 1;

dcoef(s) = pow(0.001, 1.0 / max(1.0, s * ma.SR));
ad(c)    = _ : (max ~ *(c));

penv = trig : ad(dcoef(0.02));
aenv = trig : ad(dcoef(0.22));
kick = os.osc(basef + penv * 1500) * aenv : *(dist) : ma.tanh;

// the rumble: a short feedback loop, lowpassed and re-distorted each lap
rum = kick : (+ : de.fdelay(8192, ma.SR * 0.030)
                : fi.lowpass(2, tonef)
                : *(dist * 0.35) : ma.tanh : *(fb)) ~ _;

process = (kick * 0.5 + rum * 0.8) : fi.highpass(1, 28) : fi.dcblocker
        : *(1.2) : ma.tanh : *(gain) <: _, _;
