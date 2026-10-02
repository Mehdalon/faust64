// SFX - one-shot game sounds, all synthesised. `gate` fires the sound, `kind`
// picks which. A game holds one of these and pokes the two controls; nothing
// here knows anything about a game.
import("stdfaust.lib");

gate = button("gate");
kind = nentry("kind", 0, 0, 5, 1);

dcoef(s) = pow(0.001, 1.0 / max(1.0, s * ma.SR));
ad(c)    = _ : (max ~ *(c));
trig     = gate > 0.5;

// 0 laser: fast downward sweep
env0 = trig : ad(dcoef(0.18));
s0   = os.osc(180 + env0 * env0 * 2600) * env0 : *(3) : ma.tanh * 0.5;

// 1 jump: upward square
env1 = trig : ad(dcoef(0.14));
s1   = os.square(220 + (1 - env1) * 520) * env1 * 0.35;

// 2 coin: two-tone blip
env2 = trig : ad(dcoef(0.22));
tog  = env2 > 0.55;
s2   = os.square(ba.if(tog, 988, 1319)) * env2 * 0.3;

// 3 explosion: filtered noise with a falling band
env3 = trig : ad(dcoef(0.55));
s3   = no.noise * env3 : fi.resonlp(120 + env3 * 1800, 2.5, 1) : *(4) : ma.tanh * 0.55;

// 4 hit: short noisy thump
env4 = trig : ad(dcoef(0.13));
s4   = (os.osc(70 + env4 * 300) + no.noise * 0.6) * env4
     : fi.lowpass(2, 1400) : *(3) : ma.tanh * 0.5;

// 5 powerup: rising arpeggio
env5 = trig : ad(dcoef(0.45));
stepn5 = int(0.055 * ma.SR);
idx5 = min(5, int((1.0 - env5) * 14));
s5   = os.square(330 * pow(2.0, idx5 / 4.0)) * env5 * 0.28;

sel = int(kind);
out = ba.if(sel == 0, s0,
      ba.if(sel == 1, s1,
      ba.if(sel == 2, s2,
      ba.if(sel == 3, s3,
      ba.if(sel == 4, s4, s5)))));

process = out : fi.dcblocker <: _, _;
