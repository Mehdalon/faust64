// DRONE - no sequencer, no triggers: four detuned voices and a filter that
// breathes on its own LFO. Here to show what a slow-moving DSP sounds like
// through the N64's 16-bit path, and to be the cheap one on the load meter.
import("stdfaust.lib");

root  = hslider("root",  55, 25, 220, 1);
spread= hslider("spread", 0.7, 0, 6, 0.01);
rate  = hslider("rate",  0.07, 0.01, 1, 0.01);
depth = hslider("depth", 1400, 0, 4000, 10);
base  = hslider("base",   320, 80, 3000, 10);
reso  = hslider("reso",   3.5, 0.7, 12, 0.1);
gain  = hslider("gain",   0.45, 0, 1, 0.01);

voice(m, d) = os.sawtooth(root * m + d);
mix = voice(1, 0) + voice(1, spread) + voice(2, 0 - spread) + voice(3, spread * 1.7);

lfo = os.osc(rate) * 0.5 + 0.5;
fc  = min(0.45 * ma.SR, base + lfo * depth);

process = mix / 4 : fi.resonlp(fc, reso, 1) : *(gain) : fi.dcblocker <: _, _;
