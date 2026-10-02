// The smallest useful thing: one oscillator with a level control.
import("stdfaust.lib");
process = os.osc(hslider("freq",440,50,2000,1)) * hslider("gain",0.2,0,1,0.01) <: _,_;
