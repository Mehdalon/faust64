// TUNNEL - a checker tunnel in the Game Boy's 4 shades, flying on the beat.
// faust2gb -v: one sample per pixel, 128 x 88, 29.86 pictures a second.
//
// Each pixel's angle round the centre and 1/distance are texture coordinates;
// the tunnel speeds up on every beat (132 bpm, as chipwave.dsp) and the far end
// flashes. Smooth fog: the values are not on the 4 levels, so faust2gb dithers.
//   ../../faust2gb -d 20 -v tunnel.dsp chipwave.dsp
import("stdfaust.lib");
import("vis.lib");
W = @FW@; H = @FH@; FPS = @FPS@;
bpm = 132;

process = gb_xyt(W, H, FPS) : pix;
pix(x, y, t) = max(0.0, min(1.0, c))
  with {
    beat = t * bpm / 60.0;
    bp = beat - float(int(beat));                              // 0..1 through the beat
    go = float(int(beat)) * 0.5 + 0.5 * (1.0 - exp(0.0 - 4.0 * bp));   // a lurch forward per beat
    dx = x - float(W) * 0.5 + 0.5;  dy = (y - float(H) * 0.5 + 0.5) * 1.15;
    r = sqrt(dx * dx + dy * dy) + 0.5;
    u = atan2(dy, dx) / (2.0 * ma.PI) * 8.0 + t * 0.4;
    v = 90.0 / r + go * 2.0;
    check = (int(floor(u)) + int(floor(v))) & 1;
    fog = min(1.0, r / 52.0);
    flash = exp(0.0 - 6.0 * bp) * max(0.0, 1.0 - r / 22.0);
    c = ba.if(check == 0, 1.0, 0.38) * fog + flash;
  };
