// PLASMA - the demoscene classic in 4 shades, dithered.
// faust2gb -v: one sample per pixel, 128 x 88, 29.86 pictures a second.
// Four moving sine waves summed per pixel, through a cosine.
//   ../../faust2gb -d 20 -v plasma.dsp chipwave.dsp
import("stdfaust.lib");
import("vis.lib");
W = @FW@; H = @FH@; FPS = @FPS@;

process = gb_xyt(W, H, FPS) : pix;
pix(x, y, t) = 0.5 + 0.5 * cos(v * ma.PI + t * 0.7)
  with {
    dx = x - float(W) * 0.5;  dy = y - float(H) * 0.5;
    v = sin(x * 0.07 + t * 1.1) + sin(y * 0.09 - t * 1.3) + sin((x + y) * 0.05 + t * 0.6)
      + sin(sqrt(dx * dx + dy * dy) * 0.12 - t * 1.7);
  };
