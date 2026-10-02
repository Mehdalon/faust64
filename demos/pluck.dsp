// PLUCK - Karplus-Strong strings, no samples and no oscillators: a burst of
// noise recirculating in a delay line. Physical modelling on a 1996 console.
import("stdfaust.lib");
nyq(f) = min(f, 0.45 * ma.SR);                // keep filters below Nyquist at low rates (Game Boy, ZX)

bpm   = hslider("bpm",   112, 60, 180, 1);
decay = hslider("decay", 0.93, 0.7, 0.999, 0.001);
bright= hslider("bright", 0.5, 0, 1, 0.01);
gain  = hslider("gain",  0.55, 0, 1, 0.01);

stepn = int(60.0 * ma.SR / (bpm * 2.0));
t     = ba.time;
trig  = (t % stepn) < 1;
step  = int(t / stepn) % 16;

dcoef(s) = pow(0.001, 1.0 / max(1.0, s * ma.SR));
ad(c)    = _ : (max ~ *(c));

pat = waveform{0, 7, 12, 7, 3, 10, 15, 10, 5, 12, 17, 12, 3, 10, 7, 3};
n   = pat, step : rdtable;
f   = 110.0 * pow(2.0, n / 12.0);

burst = no.noise * (trig : ad(dcoef(0.002)));
// one-zero lowpass in the loop is what makes the high partials die first
str = burst : (+ : de.fdelay(4096, ma.SR/f - 1) : fi.lowpass(1, nyq(1200 + bright*6000)) : *(decay)) ~ _;

process = str : fi.dcblocker : *(gain) <: _, _;
