// HOOVER - the Alpha Juno stab: detuned saws through a PWM'd pulse stack,
// sweeping filter, played as a rave riff. Pure Faust, self-running.
import("stdfaust.lib");

bpm   = hslider("bpm",   155, 120, 200, 1);
det   = hslider("detune", 0.9, 0, 4, 0.01);
sweep = hslider("sweep", 2600, 200, 7000, 10);
reso  = hslider("reso",    6, 1, 18, 0.1);
gain  = hslider("gain",  0.5, 0, 1, 0.01);

stepn = int(60.0 * ma.SR / (bpm * 2.0));
t     = ba.time;
trig  = (t % stepn) < 1;
step  = int(t / stepn) % 8;

dcoef(s) = pow(0.001, 1.0 / max(1.0, s * ma.SR));
ad(c)    = _ : (max ~ *(c));

riff = waveform{0, 0, 5, 3, 0, 7, 5, 3};
n    = riff, step : rdtable;
f    = 110.0 * pow(2.0, n / 12.0);

env  = trig : ad(dcoef(0.30));
fenv = trig : ad(dcoef(0.22));

// the hoover timbre: saw stack plus a slowly PWM'd pulse an octave down
pwm  = os.osc(0.9) * 0.2 + 0.4;
stack = os.sawtooth(f) + os.sawtooth(f * (1 + det/100)) + os.sawtooth(f * (1 - det/100))
      + os.pulsetrain(f/2, pwm) * 0.8;

process = stack / 4 * env
        : fi.resonlp(min(0.45*ma.SR, 300 + fenv * sweep), reso, 1)
        : *(2.2) : ma.tanh : fi.dcblocker : *(gain) <: _, _;
