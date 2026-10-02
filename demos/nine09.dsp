// NINE09 - a 909-ish kit, synthesised from scratch, run through a stompbox.
//
// The pedal is modelled the way a real overdrive is wired, and the order
// matters: highpass BEFORE the clipper (so the bass does not turn to mud),
// asymmetric soft clip (one diode pair bigger than the other, which is what
// puts even harmonics in), then a passive-style tone lowpass, then level.
// Distorting the whole kit together is also what makes the parts glue.
import("stdfaust.lib");
nyq(f) = min(f, 0.45 * ma.SR);                // keep filters below Nyquist at low rates (Game Boy, ZX)

bpm    = hslider("bpm",    128, 90, 190, 1);
drive  = hslider("drive",   14, 1, 60, 0.5);
bias   = hslider("bias",  0.18, 0, 0.6, 0.01);   // asymmetry = even harmonics
pre    = hslider("pre",    120, 20, 800, 5);     // pre-clip highpass
tone   = hslider("tone",  3200, 500, 9000, 50);  // post-clip lowpass
level  = hslider("level", 0.45, 0, 1, 0.01);
mix    = hslider("mix",   0.85, 0, 1, 0.01);     // blend back the clean kit

stepn = int(60.0 * ma.SR / (bpm * 4.0));
t     = ba.time;
trig  = (t % stepn) < 1;
step  = int(t / stepn) % 16;

dcoef(s) = pow(0.001, 1.0 / max(1.0, s * ma.SR));
ad(c)    = _ : (max ~ *(c));
hit(tb)  = trig * (tb, step : rdtable);

// --- patterns (classic four-on-the-floor with a shuffling hat) -------------
patK = waveform{1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,1};
patS = waveform{0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0};
patC = waveform{1,0,1,0, 1,0,1,1, 1,0,1,0, 1,1,1,0};
patO = waveform{0,0,0,0, 0,0,1,0, 0,0,0,0, 0,0,1,0};
patL = waveform{0,0,0,0, 0,0,0,0, 0,0,1,0, 0,0,0,0};

// --- 909 kick: long sine with a fast pitch drop and a click ---------------
kh = hit(patK);
kick = os.osc(48 + (kh : ad(dcoef(0.035))) * 1500) * (kh : ad(dcoef(0.55)))
     + (no.noise * (kh : ad(dcoef(0.003))) : fi.highpass(2, nyq(2200)) : *(0.35));

// --- 909 snare: two tuned bodies plus a noise shell ------------------------
sh = hit(patS);
se = sh : ad(dcoef(0.16));
snare = (os.osc(185) + os.osc(330) * 0.7) * se * 0.5
      + (no.noise * (sh : ad(dcoef(0.20))) : fi.bandpass(2, nyq(1400), nyq(6500)) : *(0.8));

// --- hats: bandpassed noise, closed short and open long --------------------
ch = hit(patC);
oh = hit(patO);
hats = no.noise * (ch : ad(dcoef(0.028))) : fi.highpass(2, nyq(7000)) : *(0.30)
     + (no.noise * (oh : ad(dcoef(0.30))) : fi.highpass(2, nyq(6000)) : *(0.22));

// --- clap: a short burst train, then the tail ------------------------------
lh  = hit(patL);
clap = no.noise * ((lh : ad(dcoef(0.012))) * (1 + 0.8 * os.osc(520)))
     : fi.bandpass(2, nyq(1000), nyq(3200)) : *(0.55);

kit = kick * 1.0 + snare * 0.8 + hats + clap;

// --- the pedal -------------------------------------------------------------
clip = _ : +(bias) : ma.tanh : -(ma.tanh(bias));   // asymmetric soft clipping
pedal = fi.highpass(2, nyq(pre)) : *(drive) : clip : fi.lowpass(2, nyq(tone)) : *(1.0 / (1 + drive * 0.06));

wet = kit : pedal;
out = kit * (1 - mix) + wet * mix;

process = out : fi.dcblocker : *(level) <: _, _;
