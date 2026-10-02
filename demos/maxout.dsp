// MAXOUT - the stress test. Deliberately the most expensive thing in the set:
// NV independent voices, each two detuned saws through its OWN resonant lowpass
// with its own LFO, so the filter coefficients are recomputed every sample for
// every voice. Then a feedback delay and a distortion on top.
//
// The point is to find where the VR4300 runs out, not to sound good. Watch the
// load meter on the ROM: at 100% you are underrunning. Change NV and rebuild.
import("stdfaust.lib");

NV = 5;                      // <- the knob that matters. Rebuild to change it.
                             // 5 measured at 97% on a real VR4300 @ 22050 Hz;
                             // 12 measured at 213%, i.e. silence and stutter.

spread = hslider("spread", 0.9, 0, 6, 0.01);
base   = hslider("base",   380, 80, 3000, 10);
depth  = hslider("depth", 1500, 0, 5000, 10);
reso   = hslider("reso",   6.0, 0.7, 18, 0.1);
drive  = hslider("drive",  3.0, 1, 20, 0.1);
fb     = hslider("fb",    0.45, 0, 0.9, 0.01);
gain   = hslider("gain",  0.35, 0, 1, 0.01);

voice(i) = (os.sawtooth(f) + os.sawtooth(f * (1 + spread / 100.0)))
         : fi.resonlp(fc, reso, 1)
  with {
    f   = 55.0 * pow(2.0, (i % 12) / 12.0) * (1 + int(i / 12));
    lfo = os.osc(0.11 + i * 0.037) * 0.5 + 0.5;
    fc  = min(0.45 * ma.SR, base + lfo * depth);
  };

stack = par(i, NV, voice(i)) :> /(NV * 2.0);

process = stack : *(drive) : ma.tanh
        : (+ : de.fdelay(16384, ma.SR * 0.21) : fi.lowpass(2, 2600) : *(fb)) ~ _
        : fi.dcblocker : *(gain) <: _, _;
