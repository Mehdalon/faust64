# Which Faust: the release, or our fork

Short answer: **the examples in this repository work with any recent Faust.**
Use our fork when your own program hits one of the two bugs below, or when you
work on the fixed-point path.

## Checked (2026-10-02)

Every sound program here (`demos/*.dsp`, `examples/bass.dsp`, `examples/sine.dsp`,
`game/sfx.dsp`, `gb/vis/chipwave.dsp`) was rendered with upstream Faust
(master-dev fd09e56, 2.90.0) and with the fork: the output is **byte-identical**.
The Game Boy picture build (`faust2gb -v`) passes its checks with both.

## The fork

[github.com/Mehdalon/faust](https://github.com/Mehdalon/faust), branch **`faust64`**:
upstream fd09e56 plus two fixes in the C backend, both sent to Faust as pull
requests ([grame-cncm/faust#1326](https://github.com/grame-cncm/faust/pull/1326),
[grame-cncm/faust#1327](https://github.com/grame-cncm/faust/pull/1327)). Once they
are merged, the release is enough for everything.

**1. Squares of an int and a float in one program** (#1326). Faust writes a small
helper function for `x * x` / `pow(x, 2)`, one for ints and one for floats. Both
name their argument `value`, and the compiler keeps one type per name - so in a
program that squares both an int and a float, one helper comes out wrong,
depending on which is declared last:

```faust
n = int(hslider("n", 3, 0, 9, 1));
x = hslider("x", 0.6, 0, 1, 0.01);
process = float(n * n), x * x;      // upstream: x * x gives 0, not 0.36
```

Either the float square is done with an integer multiply (0.36 becomes 0), or
the int square loses its overflow-safe multiply. The fork names the arguments
by type. None of the programs here does this; yours might.

**2. Fixed point (`-fx`) programs with a table** (#1327). Any `-fx` program with a
table - `os.osc` uses one - does not compile: the table is declared with its own
fixed-point type and the function that fills it with another. The fork declares
them alike. (`tools/fxfixup.py` patches the generated C instead, for upstream.)

## Fixed point: what it is for, and where it stands

None of the console targets here uses it: the N64, GameCube and Wii have an FPU,
and the ZX and Game Boy sound is rendered on your computer. Fixed point is the
path to consoles **without** an FPU (Game Boy Advance, PlayStation, Mega Drive,
...), and it is **not usable for sound yet**, with either compiler:

- `lib/faustfx.h` (the 32-bit fixed-point layer for `-fx -fx-size 32`) is
  complete and unit-tested;
- but Faust's range analysis cannot bound a feedback signal, so it gives
  recursive signals no fractional bits: an oscillator's phase can only hold
  whole numbers and the oscillator is silent. `tools/fxcheck.sh examples/sine.dsp`
  (float vs fixed render) reports **correlation 0.000**. Upstream does not get
  that far - it stops at the table (bug 2).
- `demos/acid.dsp` and `examples/bass.dsp` do not compile with `-fx` at all, with
  either compiler (errors around `tanhl` and casts of fixed-point values).

Details, measurements and the possible ways forward: [FIXEDPOINT.md](../FIXEDPOINT.md).

## Building the fork

Needs git, CMake and a C++ compiler (about a minute on a recent Mac; no LLVM).

```bash
git clone --branch faust64 --recursive https://github.com/Mehdalon/faust.git faust-faust64
cd faust-faust64/build
mkdir faustdir && cd faustdir
cmake -C ../backends/light.cmake -DINCLUDE_DYNAMIC=OFF -DINCLUDE_STATIC=OFF ..
make -j8 faust
export FAUST="$PWD/../bin/faust"     # the scripts here use $FAUST
"$FAUST" --version
```

`--recursive` also fetches the Faust libraries (`stdfaust.lib` and the rest),
which the compiler needs.
