// A reese-ish bass: two detuned saws through a resonant lowpass.
// Deliberately not trivial - it exists to put real load on the VR4300.
import("stdfaust.lib");

freq   = hslider("freq",   55, 20, 400, 1);
detune = hslider("detune", 0.6, 0, 8, 0.05);
cutoff = hslider("cutoff", 900, 60, 8000, 10);
q      = hslider("res",    4, 0.7, 20, 0.1);
drive  = hslider("drive",  1.5, 1, 8, 0.1);
gain   = hslider("gain",   0.3, 0, 1, 0.01);

osc = os.sawtooth(freq) + os.sawtooth(freq + detune) + os.sawtooth(freq * 0.5);
process = osc / 3 : *(drive) : ma.tanh : fi.resonlp(cutoff, q, 1) : *(gain) <: _,_;
