# Fixed point for Faust on CPUs — what was built, and what blocks it

Written 2026-09-21. Everything here is measured.

## The goal

Every console worth targeting that is not the N64, GameCube or Wii has **no
FPU**: Game Boy Advance (ARM7TDMI), PlayStation (R3000A), Mega Drive (68000),
Mega 32X (SH-2), PC Engine, Super Famicom. Faust's `-lang c` output is float,
so reaching any of them needs a fixed-point path.

## What was built, and works

**`lib/faustfx.h`** — a CPU fixed-point layer for Faust's `-fx` output, with no
Xilinx dependency.

Faust's own `faust/dsp/fixed-point.h` is built on `ap_fixed`, Xilinx's
arbitrary-precision type for **FPGA synthesis**. It is C++ only, `ap_fixed.h`
is not shipped with Faust, and its math helpers convert through `float`
anyway. With plain `-fx`, Faust gives every variable its own width from range
analysis and those reach **239 bits** — synthesisable in fabric, useless on a
68000.

The opening is `-fx-size 32`, which caps every type at 32 bits and leaves only
the binary point differing per variable. That is an `int32_t` with a
compile-time scale. `lib/faustfx.h` implements exactly that:

- `fxp<M,L>` holding an `int32_t`, meaning `raw * 2^L`
- operators returning an `fxi<L>` intermediate carrying an `int64_t`, so a
  multiply stays exact until it is stored
- saturation to the target width, matching `AP_SAT`
- `(int)` casts that **truncate toward zero**, as C does — caught by unit test,
  and it matters because Faust uses those casts for table indices and counters
- `fmaxfx`/`fminfx`/`fabsfx` as pure integer ops
- transcendentals through `double`, deliberately: measured on `demos/acid.dsp`,
  the per-sample loop calls `pow` twice and `tan` once *only* because its
  filter cutoff is modulated per sample. A static cutoff puts none in the loop.

Verified by unit test (`/tmp/fxunit.cpp` pattern): add, subtract, multiply,
divide, min, max, abs, int conversion and round-trip all correct.

**`tools/fxcheck.sh`** — renders a DSP twice on the host, float and fixed, and
reports correlation, error RMS and SNR. The correctness test, no console
needed.

**`tools/fxfixup.py`** — works around a genuine bug in Faust's fixed-point C
backend: it declares wavetables as `sfx_t(m,l) ftbl...[]` but declares the
helper that fills them as taking `fixpoint_t*`. Two different types, so the
call does not compile. **This would fail against `ap_fixed` too.**

## What blocks it — in Faust, not in the layer

**Faust's interval analysis cannot bound a recursive signal.** It assumes any
feedback signal may reach 2^31 and spends the whole word on integer bits.

Measured, `-fx-size 32`:

| DSP | max integer bits | types with ZERO fractional bits |
|---|---:|---:|
| non-recursive reference | 1 | 0 of 4 |
| `examples/sine.dsp` | 30 | **17 of 35** |
| `demos/drone.dsp` | 30 | **133 of 241** |
| `demos/pluck.dsp` | 30 | **56 of 114** |

What that does to a DSP, from the generated sine:

```c
sfx_t(30,0) fTemp0 = fSlow1 + dsp->fRec1[1];   // L=0: no fractional bits
dsp->fRec1[0] = fTemp0 - floorfx(fTemp0);      // therefore always 0
```

The phase accumulator can only hold integers, so `x - floor(x)` is zero and
the oscillator is silent. `tools/fxcheck.sh` on `examples/sine.dsp` reports
**correlation 0.000, fixed RMS 0.000** against a float RMS of 0.141 — not
approximation error, total collapse. Every audio DSP worth running
(oscillators, filters, envelopes, delays) is recursive.

Three attempts to get round it, all measured:

1. **`assertbounds(lo, hi, x)`** — the primitive exists and parses, and would
   be the correct fix. It **crashes the fixed-point backend**:
   `ASSERT : unrecognized signal : sigAssertBounds ... please report this
   message ... to Faust developers` (signalVisitor.cpp:223, Faust 2.85.9).
   **This is worth reporting upstream.**
2. **`-wi` / `-ni`** (widening / narrowing iterations of the signal bounding).
   Move the needle slightly — sine goes from 17 to 14 zero-fraction types —
   but `max m` stays at 30. Not a fix.
3. **`-msoft-float`** on the N64, to find out what software float actually
   costs on a MIPS CPU and therefore whether PlayStation could skip fixed
   point entirely. **Cannot be measured on this toolchain**: the build links
   and then crashes with `Floating point invalid operation` inside
   `__divsf3`, from `libgcc/config/hardfp` — libdragon's libgcc is built
   hardware-float only, so "soft float" code still executes `div.s`.

## Where that leaves it

`lib/faustfx.h` is necessary and not sufficient. It is correct, tested, and
ready for the day the front end produces usable formats. The blocker is one
level up.

Routes, in the order I would take them:

1. **Report `sigAssertBounds` to the Faust developers.** It is a clean
   reproduction, it is their own assert asking to be told, and it is the
   feature that unlocks this properly.
2. **Bound the recursion by construction.** A DSP written so every feedback
   path passes through an explicit saturating clamp might survive the interval
   analysis. Untested, and it constrains how the DSP is written, but it needs
   nothing from upstream.
3. **Measure software float on a real FPU-less target** with a toolchain whose
   libgcc is actually soft-float. PlayStation at 33 MHz is the best candidate;
   if soft float turns out to be affordable at 8–11 kHz, none of this is
   needed for that machine.
