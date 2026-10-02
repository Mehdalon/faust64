// ACID - a 303 that plays itself. 16 steps, slide, accent, screaming filter.
// Self-running: no controls are needed to hear it, but they are all live.
import("stdfaust.lib");

bpm    = hslider("bpm",     148, 90, 200, 1);
cutoff = hslider("cutoff",  520, 100, 6000, 10);
reso   = hslider("reso",     14, 1, 24, 0.1);
envmod = hslider("envmod", 3200, 0, 8000, 10);
decay  = hslider("decay",  0.28, 0.05, 1.2, 0.01);
drive  = hslider("drive",   3.2, 1, 12, 0.1);
gain   = hslider("gain",    0.6, 0, 1, 0.01);

// --- clock -----------------------------------------------------------------
stepn  = int(60.0 * ma.SR / (bpm * 4.0));      // samples per 16th
t      = ba.time;
trig   = (t % stepn) < 1;
step   = int(t / stepn) % 16;

// --- one-pole AD envelope from an impulse ----------------------------------
dcoef(s) = pow(0.001, 1.0 / max(1.0, s * ma.SR));
ad(c)    = _ : (max ~ *(c));

// --- pattern ---------------------------------------------------------------
notes = waveform{0, 0, 12, 0, 3, 0, 0, 10, 0, 3, 7, 0, 0, 15, 10, 3};
accs  = waveform{1, 0,  0, 0, 1, 0, 0,  0, 1, 0, 0, 0, 1,  0,  0, 0};
gates = waveform{1, 0,  1, 0, 1, 1, 0,  1, 1, 1, 1, 0, 1,  1,  1, 1};

note = notes, step : rdtable;
acc  = accs,  step : rdtable;
gate = gates, step : rdtable;

freq  = 55.0 * pow(2.0, note / 12.0);
sfreq = freq : si.smooth(0.9992);              // slide between steps

hit  = trig * gate;
aenv = hit : ad(dcoef(decay));
fenv = hit : ad(dcoef(decay * 0.6));
accv = hit * acc : ad(dcoef(0.12));

fc = min(0.48 * ma.SR, cutoff + fenv * envmod + accv * 2400);

voice = os.sawtooth(sfreq) * aenv
      : fi.resonlp(fc, reso + accv * 6, 1)
      : *(drive * (1 + accv))
      : ma.tanh;

process = voice * gain <: _, _;
