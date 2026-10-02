// MASTER - the last thing in the chain, after whichever engine is selected.
// 2 in, 2 out. A sweepable lowpass with resonance, a highpass to clear the
// bottom, a drive stage, the output volume, and a true-peak limiter that has
// the last word.
//
// The limiter is not decoration. tanh bounds the drive stage, but a resonant
// filter, a feedback delay or an engine's own gain can all put more than full
// scale into it, and everything past that point hard-clips at the 16-bit rail.
// A feedforward peak limiter with instantaneous attack cannot overshoot,
// because the gain it applies is computed from the very sample it is applied
// to - no lookahead needed, and no transient slips through.
import("stdfaust.lib");

cut   = hslider("cut [unit:Hz] [scale:log]", 12000, 200, 15000, 10);
res   = hslider("res", 1.0, 0.7, 12, 0.1);
hp    = hslider("hp [unit:Hz] [scale:log]", 20, 15, 2000, 1);
drive = hslider("drive", 1.0, 1, 12, 0.1);
vol   = hslider("vol", 0.9, 0, 1, 0.01);
// checkbox defaults to 0, so the control is phrased as a BYPASS: out of the
// box the limiter is engaged, which is the safe way round
nolim = checkbox("no limit");
rel   = hslider("release [unit:s]", 0.15, 0.02, 1.0, 0.01);

lceil = 0.97;                       // leave a little room for the DC blocker

limiter = _ <: (_, (abs : hold)) : rescale
  with {
    rc      = pow(0.001, 1.0 / max(1.0, rel * ma.SR));
    hold    = max ~ *(rc);          // peak follower, fast attack, slow release
    rescale(x, e) = x * min(1.0, lceil / max(e, 1e-9));
  };

chain = fi.highpass(2, hp)
      : fi.resonlp(min(0.45 * ma.SR, cut), res, 1)
      : *(drive) : ma.tanh
      : fi.dcblocker : *(vol)
      : (_ <: limiter, _ : select2(nolim > 0.5));

process = chain, chain;
