// CHIP - a Game Boy-style tune written for the 3-bit output: no added noise.
// Two pulse voices whose levels add up exactly to the Game Boy's 8 volume steps
// (lead 0-4, bass 0-3), so the output is already 3-bit and faust2gb's encoder
// passes it through unchanged (no dither, no hiss). Stepped volume envelope,
// like the real chip's.
//   ./faust2gb -d 20 demos/chip.dsp
import("stdfaust.lib");

bpm   = hslider("bpm", 132, 80, 200, 1);
duty  = hslider("duty", 0.25, 0.125, 0.5, 0.125);

stepn = int(60.0 * ma.SR / (bpm * 4.0));       // samples per 16th
t     = ba.time;
pos   = t % stepn;
step  = int(t / stepn) % 32;
bar   = int(t / (stepn * 8)) % 4;

ph(f)       = (+(f / ma.SR) : ma.frac) ~ _;     // phase 0..1
pulse(f, d) = float(ph(f) < d);

// lead: A minor, -1 = rest; semitones above A3 (220 Hz)
leadt = waveform{12, -1, 15, -1, 19, 17, 15, -1, 12, -1, 10, 12, -1, -1, 14, -1,
                  8, -1, 12, -1, 15, 14, 12, -1, 10, -1,  7, 10, -1, -1, -1, -1};
ln    = leadt, step : rdtable;
lf    = 220.0 * pow(2.0, float(ln) / 12.0);
llev  = ba.if(ln < 0, 0, max(1, 4 - int(pos / (stepn / 4.0))));   // 4,3,2,1 within a step
lead  = pulse(lf, duty) * float(llev);

// bass: root per half bar, square wave, short gap at the end of each 16th
roots = waveform{0, -4, -7, -5};
rt    = roots, bar : rdtable;
bf    = 110.0 * pow(2.0, float(rt) / 12.0);
blev  = ba.if(pos < int(stepn * 0.75), 3, 0);
bass  = pulse(bf, 0.5) * float(blev);

process = (lead + bass) * (2.0 / 7.0) - 1.0 <: _, _;   // level 0..7 -> -1..1
