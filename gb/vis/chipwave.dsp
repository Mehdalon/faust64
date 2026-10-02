// CHIPWAVE - demos/chip.dsp's tune, plus a second output that bends the picture.
// faust2gb: scx
//
// For faust2gb -v the sound runs at one sample per screen line, so a second
// output is a value PER LINE: the line's horizontal scroll (pixels / 128). Here
// a ripple travels down the picture and kicks on every beat - same bpm, same
// clock as the tune, so the picture moves with the music.
//   ../../faust2gb -d 20 -v tunnel.dsp chipwave.dsp
import("stdfaust.lib");
chip = component("../../demos/chip.dsp");

bpm   = hslider("bpm", 132, 80, 200, 1);       // keep equal to chip.dsp's
depth = hslider("ripple px", 6, 0, 16, 0.5);

beatn = int(60.0 * ma.SR / bpm);               // samples (= lines) per beat
pos   = float(ba.time % beatn) / float(beatn); // 0..1 through the beat
kick  = exp(0.0 - 5.0 * pos);                  // 1 on the beat, dying away
ripple = sin(2.0 * ma.PI * float(ba.time) / 37.0) * depth * (0.25 + 0.75 * kick);
process = chip : _, ! : _, ripple / 128.0;
