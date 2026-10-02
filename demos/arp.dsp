// ARP - three voices that play themselves: a pulse arpeggio, a square bass and
// a noise hat. The nearest thing here to a chiptune, and the cheapest demo to run.
import("stdfaust.lib");
nyq(f) = min(f, 0.45 * ma.SR);                // keep filters below Nyquist at low rates (Game Boy, ZX)

bpm  = hslider("bpm",  140, 80, 200, 1);
duty = hslider("duty", 0.28, 0.05, 0.5, 0.01);
bri  = hslider("bri",  2600, 300, 8000, 10);
gain = hslider("gain", 0.5, 0, 1, 0.01);

stepn = int(60.0 * ma.SR / (bpm * 4.0));
t     = ba.time;
trig  = (t % stepn) < 1;
step  = int(t / stepn) % 16;
bar   = int(t / (stepn * 16)) % 4;

dcoef(s) = pow(0.001, 1.0 / max(1.0, s * ma.SR));
ad(c)    = _ : (max ~ *(c));

// arpeggio: a minor triad walked up two octaves, transposed once a bar
arpt  = waveform{0, 3, 7, 12, 15, 19, 24, 19, 15, 12, 7, 3, 0, 7, 12, 19};
roots = waveform{0, -2, 3, -4};
arpn  = arpt, step : rdtable;
root  = roots, bar : rdtable;

af = 220.0 * pow(2.0, (arpn + root) / 12.0);
ae = trig : ad(dcoef(0.10));
arpv = os.pulsetrain(af, duty) * ae : fi.lowpass(2, nyq(bri)) : *(0.35);

// bass: root note, on every 4th step
bhit = trig * ((step % 4) == 0);
be   = bhit : ad(dcoef(0.22));
bf   = 55.0 * pow(2.0, root / 12.0);
bass = os.square(bf) * be : fi.lowpass(2, nyq(700)) : *(0.5);

// hat: offbeat noise
hhit = trig * ((step % 2) == 1);
he   = hhit : ad(dcoef(0.035));
hat  = no.noise * he : fi.highpass(2, nyq(5500)) : *(0.22);

process = (arpv + bass + hat) : fi.dcblocker : *(gain) <: _, _;
