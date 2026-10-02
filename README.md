# faust64 - Faust for game consoles

Compile a [Faust](https://faust.grame.fr) `.dsp` file for old game hardware:

| target | how | sound |
|---|---|---|
| **Nintendo 64** - `faust2n64` | the DSP runs live on the console (the VR4300 has an FPU); every Faust control becomes a fader on screen, played with the controller | 22050 Hz stereo |
| **GameCube / Wii** - `faust2gc` | live, through libogc's audio | 48000 Hz stereo |
| **ZX Spectrum** - `faust2zx` | rendered on your computer, played back by a 33-byte player that toggles the 1-bit speaker | 6986 Hz, 5-bit PWM |
| **Game Boy** - `faust2gb` | rendered on your computer, streamed by a hand-assembled player through the master volume register, from an MBC5 cartridge. With `-v`, a second Faust program draws the pictures: 128 x 88, 4 shades, 29.86 a second, plus per-line raster effects from the sound program | 8192 Hz, 3-bit PCM (9198 Hz with pictures) |

```bash
./faust2n64 demos/acid.dsp        # -> demos/acid.z64   (needs libdragon, $N64_INST)
./faust2gc  demos/acid.dsp        # -> demos/acid.dol   (needs devkitPro)
./faust2zx  demos/acid.dsp        # -> demos/acid.tap + acid-preview.wav
./faust2gb  demos/chip.dsp        # -> demos/chip.gb  + chip-preview.wav
./faust2gb -d 20 -v gb/vis/tunnel.dsp gb/vis/chipwave.dsp   # Game Boy: pictures + sound, both Faust
```

**Step-by-step guide - installing, running the examples, making your own sound
and Game Boy picture programs: [docs/howto.md](docs/howto.md).**

Ready-built ROMs to try at once are in `roms/`. **Setting up an emulator so the
sound has no lag or crackle: [docs/emulators.md](docs/emulators.md).**

The ZX and Game Boy ROMs are checked without the hardware: `zx/simulate.py`,
`gb/simulate.py` and `gb/simv.py` run the players' exact timing and compare the
result with the Faust renders (for the pictures: every screen line). The Game Boy
ROMs run in SameBoy (pictures checked there, including how much timing margin the
picture copy has); their sound has been checked by simulation, not yet by
recording an emulator, and nothing has run on real hardware - try it and tell us.

**Faust version.** The examples here work with any recent Faust (checked: identical
output with upstream 2.90 and with our fork). Our fork, branch `faust64` of
[Mehdalon/faust](https://github.com/Mehdalon/faust/tree/faust64), adds two C-backend
fixes, also sent upstream as pull requests: squares of an int and a float in one
program (upstream can compute `x * x` as 0), and fixed-point (`-fx`) tables. When you
need it, how to build it, and where fixed point stands (not usable for sound yet):
[docs/faust-version.md](docs/faust-version.md).

## How it works

Faust's `-lang c` backend already emits a plain C `compute()` loop with no
runtime and no dependencies beyond `math.h`. The VR4300 has a hardware
single-precision FPU, so that output runs **unmodified** — there is no
fixed-point conversion and no transpilation step.

What this project supplies is the architecture file (`arch/n64.c`) that Faust
splices its generated code into:

- an implementation of Faust's `UIGlue` callback table that flattens the
  control tree into an array, so `hslider`/`nentry`/`button`/`checkbox` become
  on-screen, controller-editable parameters with no extra work per DSP;
- the audio loop — per-channel float scratch buffers into `computeXXX()`, then
  clamp/scale/interleave into libdragon's 16-bit stereo AI buffers;
- a live CPU load meter, because whether a given DSP fits the frame budget is a
  per-DSP question that has to be measured, not assumed.

`faust2n64` is the driver: it runs `faust -a arch/n64.c`, writes a libdragon
Makefile, and builds the ROM.

### Controls on the console

```
up/down   select a parameter
left/right adjust      (hold Z, L or R for 10x steps)
A         reset the selected parameter to its Faust default
START     reset everything, and clear the peak-load reading
```

## The audio lab

Building a ROM and booting an emulator is a bad way to answer "does this sound
right?". The lab answers it in a second, through the **same signal path** the
ROM uses - the same clamp, the same x32000, the same 16-bit stereo, at the same
rate - so the audition is honest rather than flattering.

```bash
lab/labserver.py                       # -> http://127.0.0.1:8791/
```

Pick a DSP, move the sliders, hit **render & play**. Tick *sweep* on a control
to move it linearly across the render (the fastest way to hear what a filter
actually does). **build .z64** turns the current DSP into a ROM without leaving
the page. Peak, dBFS, RMS and clipped-sample count are printed on every render.

There is a command-line form too, if you would rather stay in the shell:

```bash
lab/faustlab examples/bass.dsp                      # render 8s and play it
lab/faustlab -l examples/bass.dsp                   # list the controls
lab/faustlab -d 12 -p freq=41 -p cutoff=400 examples/bass.dsp
lab/faustlab -s cutoff=200:4000 examples/bass.dsp   # sweep
```

**The lab is verified against the ROM, not assumed equal to it.** The same 4096
samples, rendered in three places:

| | sum | min | max |
|---|---|---|---|
| lab (`lab/faustlab`) | -553406 | -18816 | 8620 |
| host reference (`tools/selftest.sh`) | -553406 | -18816 | 8620 |
| N64 ROM (VR4300) | -553407 | -18816 | 8620 |

Measured 2026-09-21. Rechecked 2026-10-02 with Faust 2.90 (release and our fork
alike): `tools/selftest.sh examples/bass.dsp` now prints sum -553414 - the exact
sum moves with the compiler version, min and max did not; compare the three
with one compiler.

One caveat worth knowing: **slider positions in the lab are not baked into the
ROM.** The ROM always starts at the DSP's own defaults. When you find settings
you like, write them into the `.dsp`.

## Demos

Fourteen self-playing tracks in `demos/`, **all of them pure Faust** - no C, no
samples, no data files. Every sequencer, every drum, every distortion stage is
written in the DSP language itself. Each builds to its own ROM:

```bash
for f in demos/*.dsp; do ./faust2n64 "$f"; done
```

| demo | what it is |
|---|---|
| `drone` | four detuned voices and a filter breathing on its own LFO. No triggers at all. |
| `arp` | pulse arpeggio, square bass, noise hat. The nearest thing here to a chiptune. |
| `fm` | two-operator FM bells with a moving index. |
| `pluck` | Karplus-Strong strings - noise recirculating in a delay line. Physical modelling on a 1996 console. |
| `acid` | a 303 that plays itself: 16 steps, slide, accent, screaming filter. |
| `hoover` | the Alpha Juno stab, played as a rave riff. |
| `schranz` | 150 BPM, and the "sampled loop" is comb-filtered noise through a swept bandpass. |
| `hardkick` | pitch-enveloped kick with a distorted rumble tail. |
| `gabberkick` | 190 BPM, overdriven twice - the second distortion after the lowpass is where the grind comes from. |
| `frenchcore` | 205 BPM, **tuned** kick tail playing a note, with an acid lead on top. |
| `rumble` | hardtekk: the tail is fed back through a lowpass and re-distorted each lap. |
| `distbass` | neuro reese - drive first, *then* the moving notch. Distorting after the filter only buzzes. |
| `nine09` | a 909-ish kit through a stompbox: highpass before the clipper, asymmetric soft clip for even harmonics, passive-style tone lowpass after. |
| `maxout` | the stress test. Exists to find the cliff, not to sound good. |


## How much of the console each one costs

Two instruments, and **they disagree** — which is itself the most useful thing
measured here.

The **live meter** is the number on the running ROM's own screen: what that DSP
actually costs while the console is also driving video, servicing interrupts
and streaming audio out. The **bench** (`tools/bench.sh`) times every demo
back-to-back in one ROM. Both at 16000 Hz:

| demo | live | bench | demo | live | bench |
|---|---:|---:|---|---:|---:|
| drone | 20% | 13% | schranz | 48% | 42% |
| hardkick | 23% | 28% | frenchcore | 51% | 87% |
| pluck | 24% | 20% | arp | 59% | 34% |
| acid | 27% | 22% | hoover | 60% | 35% |
| gabberkick | 28% | 45% | distbass | 61% | 45% |
| rumble | 31% | 28% | maxout (5 voices) | 82% | 83% |
| fm | 34% | 19% | nine09 | 46% | 61% |

**Trust the live column.** The bench disagrees by up to 25 points in *both*
directions, and the reason is not subtle: inside one ROM, where each DSP's code
and data happen to land dominates the measurement. Proven by building the same
DSP eight ways into one sweep ROM — two variants that should have been
near-identical came out at **2200 and 954 cycles per sample**, a 2.3x spread no
compiler flag can explain. `tools/optsweep.sh` now builds **one ROM per
variant** for exactly this reason.

`maxout` exists to find the cliff. At 12 voices it measured **213%** — the
console cannot make those samples in the time they have to play. At 5 it sits
at 82% live.

### The bench used to lie about anything that decays

It timed 4096 samples **straight from reset** — a state a DSP is almost never
in. Envelopes had not decayed, delay lines were empty, nothing had reached the
denormal range. That made `frenchcore` read 90% when it is 51% live. It now
settles each DSP for a second of audio first.

### Four things measurement found

None predicted; all four came from watching the ROMs run, or from reading
what the compiler actually emitted.

**1. Denormals.** `arp` read 83% CPU live against 59% with Faust's `-ftz 1`.
The VR4300 traps denormal operands into software, and every decaying envelope
eventually produces them. `-ftz 1` is the default in `faust2n64`,
`lab/faustlab` and `tools/bench.sh` (pass `-F` to turn it
off and hear it). `-ftz 2` emits C++ syntax the C backend cannot compile.
**Caveat, stated honestly:** the isolated per-variant bench does *not*
reproduce that 24-point win - an open question.

**2. The load meter was timing the console idling.** `audio_write_begin()`
blocks until the AI frees a buffer, and it sat inside the timed region, so the
reported load drifted towards 100% however cheap the DSP was.

**3. A controller nobody touched walked every parameter to its limit.**
A pad test counted **19 lefts, 21 rights and 19 ups from four keypresses**:
the loop polled the pad ~80,000 times a second against a device that updates
60 times a second, and `joypad_get_buttons_pressed()` manufactured edges. Now
polled at 250 Hz — faster than a human press, slower than an SI transaction.

### 4. Every Faust oscillator was making a libm call per sample

Found while building the tracker, and it is the widest-reaching of the four.

`os.sawtooth`, `os.osc` and `os.pulsetrain` all sit on a phasor built from
`ma.frac`, and `ma.frac(x)` is `x - floor(x)`. On the VR4300 `floorf` is not an
instruction, it is a **call** — checked by compiling it for the actual target:

```
$ mips64-elf-gcc -O2 -march=vr4300 -S   # float f(float x){return floorf(x);}
f:  j  floorf
```

One call per oscillator per sample. Across six tracker voices that is up to
eight libm calls a sample, and **nothing in the `.dsp` mentions `floor`** —
it is invisible unless you disassemble. This is the same shape as the `powf`
that made the wobble demo eight times more expensive than it needed to be.

The phasor accumulator only ever advances by `f/SR < 1`, so it can never be
two or more past the top: wrapping is a compare and a subtract, with no
conversion, no call and no branch. The project's `daw.lib` (not in this repo) provides `dw_phasor`,
`dw_saw`, `dw_pulse`, `dw_sin` (a 2048-point table, about −66 dB THD) and
`dw_saws2`. All six tracker voices have **zero library calls in their sample
loops**.

Worth checking on your own DSPs — count the calls in the generated C's sample
loop before optimising anything else:

```bash
faust -lang c -ftz 1 -cn v -o /tmp/v.c yours.dsp
sed -n '/for (i0 = 0; i0 < count/,$p' /tmp/v.c | grep -c 'powf\|tanhf\|floorf\|expf\|tanf'
```

Two theories measurement **killed**, recorded so they are not tried again:
throttling the console redraw (91% → 83%, not the cause) and chunking compute
to fit the 8 KB D-cache (80% → 79%, not the cause).

## The interface builds itself

`ui/faustui.h` and `ui/faustgui.h` turn whatever the DSP declares into an
actual interface - faders, knobs, buttons, checkboxes, meters - with no
per-DSP code anywhere. Add a slider to a `.dsp` and a fader appears; mark it
`[style:knob]` and it becomes a knob.

It follows Faust's own semantics rather than flattening them:

- **groups nest** - `vgroup` stacks vertically, `hgroup` places side by side
- **`tgroup` becomes real tabs**, and only the open one is drawn
- **bargraphs are output-only**, so they are drawn as live meters and skipped
  by the cursor
- **`[unit:Hz]`** is shown next to the value, **`[scale:log]`** makes the fader
  move by ratio (a linear step makes a log control unusable at one end),
  **`[hidden]`** is respected
- a `vgroup` too tall for a 240-line screen **wraps into a second column**
  rather than being clipped - the one liberty taken with Faust's layout


Everything is inset from the framebuffer edge, because a CRT does not show the
whole thing.

## Using it in a game

`game/faust64.h` is the same DSP engine with the slider page taken off: music
that loops, sound effects you fire, a master chain, one call per frame.

```c
F64_DECLARE(e_music); F64_DECLARE(e_sfx); F64_DECLARE(m_master);

f64_init(16000, 4);
f64_music(&eM); f64_sfx_engine(&eS); f64_master(&eX);

while (1) {
    if (jumped)  f64_sfx(F64_SFX_JUMP);
    if (diving)  f64_set(F64_MASTER, "cut", 800);   // any control, by its label
    f64_update();                                   // the whole audio system
}
```

`game/sfx.dsp` carries six one-shots (laser, jump, coin, boom, hit, powerup)
picked by a `kind` control — pure Faust, no samples. `roms/demo.z64` is a
playable example.

Two things that are load-bearing:

- **`f64_update()` is bounded.** It tops up at most every buffer the AI has,
  once. An unbounded `while (audio_can_write())` inside a frame function hung
  the demo dead — black screen, all init completed.
- **`f64_set()` returns 0 if there is no such control.** A typo in a label is
  otherwise completely silent.

## ZX Spectrum

```bash
./faust2zx demos/acid.dsp          # -> acid.tap  +  acid-preview.wav
python3 zx/tap2wav.py demos/acid.tap acid-tape.wav   # audio for a REAL Spectrum
```

This target works differently from every other one here, and the difference is
the point: **the Spectrum has one bit of audio.** Bit 4 of port 0xFE drives the
speaker, and a 3.5 MHz Z80 has to bang it by hand. So the DSP does not run on
the machine - it is rendered on the host and encoded as pulse widths, and the
Z80 does nothing but toggle that bit.

Which also means **this is the one target that needs no fixed-point layer**:
the output is a bit, not a number. See FIXEDPOINT.md for why that matters.

### The timing is counted, not estimated

Pulse width modulation only holds pitch if every sample takes the SAME number
of T-states, so the player is two DJNZ delays whose lengths always sum to a
constant:

```
fixed work   95 T
delays       13*(W-1)+8  +  13*(LEVELS-W-1)+8  =  13*(LEVELS-2)+16 = 406 T
total       501 T per sample, whatever W is
            3500000 / 501 = 6986.0 Hz
```

At 32 levels that is 5 bits of amplitude at 6986 Hz. The player is **33 bytes**.

W is held in 1..LEVELS-1 by the encoder, because a DJNZ count of zero on a Z80
means 256 iterations rather than none.

### Verified without a Spectrum

`zx/simulate.py` reproduces what the speaker does - the mean of the two-level
signal over one period is exactly W/LEVELS - and correlates it against the
audio that went in:

| demo | correlation | SNR of the 5-bit encode |
|---|---:|---:|
| acid | 0.9957 | 20.4 dB |
| hardkick | 0.9979 | 23.1 dB |
| gabberkick | 0.9975 | 22.5 dB |

The encoder uses error diffusion, which moves the quantisation noise up out of
the way instead of leaving it as a buzz sitting on the signal.

### Real hardware

`zx/tap2wav.py` writes the tape audio: 2168 T pilot, 667/735 sync, 855/1710
bit cells, MSB first - the ROM loader's timings, not preferences. Measured
output is **1437 baud**, against the ~1500 the ROM expects. Play it into the
EAR socket and type `LOAD ""`.

### The honest limit

One byte per sample at 6986 Hz, loading at 0x6000 to leave BASIC its ~800
bytes, gives **40959 bytes = 5.86 seconds** on a 48K. That is the whole
machine. A 128K with paging is the obvious way to more.

## GameCube and Wii

```bash
./faust2gc demos/acid.dsp          # -> acid.dol   GameCube
./faust2gc --wii demos/acid.dsp    # -> acid.dol   Wii
tools/play.sh demos/acid.dol             # -> Dolphin
```

Works for the same reason the N64 does: PowerPC has a hardware FPU, so Faust's
`-lang c` output runs unmodified. Audio goes out through libogc's ASND as a
double-buffered stereo voice, and `ui/faustui.h` ported with no changes at all
because it is plain `printf`.

### Run at 48000, not 32000

asndlib **fixes the hardware mixer at 48 kHz** (`INIT_RATE_48000`). A voice at
any other rate is resampled inside the library, and that resampling - not the
DSP - is what made a 32000 Hz build sound rough. 48000 is now the default and
nothing is resampled.

This took a while to find because it is indistinguishable by ear from a
starved audio loop. What separated them was putting the numbers on screen:

```
underruns 0   realtime lag 18 ms
```

`underruns` counts the times the ASND callback came round and the main loop
had not refilled the other block - a direct, unambiguous count. Zero means the
ROM is keeping up, so the fault had to be downstream, which pointed straight
at the library. (The 18 ms was an artefact of the meter not counting the two
blocks pre-filled before the clock started; that offset is now seeded.)

## Options

```
-o FILE   output ROM path                      (default: <name>.z64)
-r RATE   sample rate                          (default: 22050)
-b N      number of audio buffers              (default: 4)
-t TITLE  ROM header title, <= 20 chars
-S        build the self-test ROM (see below)
-k        keep the generated C and build directory
-X ARG    pass ARG straight to faust, e.g. -X -vec
```

## Verified, 2026-09-21

Not "it works" — the numbers, on `examples/bass.dsp` (three detuned saws,
tanh drive, resonant lowpass, 6 controls):

| check | result |
|---|---|
| ROM builds | 147,456 bytes; 122,872 text / 24,628 data / 3,796 bss |
| boots in ares | yes, 59 VPS |
| controls auto-discovered | 6/6, from the Faust `hslider` declarations alone |
| audio | AI at 22047 Hz, 880 frames/buffer, 0 in / 2 out |
| CPU load | **15%** steady at 22050 Hz |

**Numeric correctness** was checked rather than assumed. `tools/selftest.sh`
renders 4096 samples from the reset state twice — once on the host from Faust's
generated C, once on the N64 from the same generated C — and prints a
fingerprint of both:

|  | sum | min | max |
|---|---|---|---|
| host (x86, `cc -O2`) | -553406 | -18816 | 8620 |
| N64 (VR4300, `-O2 -ffast-math`) | -553407 | -18816 | 8620 |

Identical extremes; the sum differs by 1 LSB across 4096 samples, which is one
sample rounding differently under `-ffast-math` on a different FPU. That is the
expected result, not a defect.

```bash
./tools/selftest.sh examples/bass.dsp        # prints host numbers, builds the ROM
```

## Honest limits

- **No audio input.** The N64 has none. A DSP with inputs compiles and runs, but
  those inputs are fed silence.
- **The CPU budget is real and per-DSP.** 15% for the reese bass at 22050 Hz
  says nothing about a 12-voice FM synth. Watch the load meter; at 100% you are
  underrunning. Lower `-r` first, then simplify the DSP.
- **Everything runs on the VR4300.** The RSP — the N64's actual vector DSP — is
  untouched. A Faust backend targeting RSP microcode is the genuinely
  interesting version of this project and is not what this is.
- **Bargraphs are display-only** and are currently dropped, not drawn.
- **`soundfile` primitives are not supported.**
- **The lab needs no N64 toolchain**, but its *build .z64* button does.
- Controls are capped at 32 (`-DFAUST_N64_MAXPARAM`) and the console shows as
  many as fit on screen.

## Requires

- `faust` 2.x with the C backend (`brew install faust`)
- a libdragon toolchain, found via `$N64_INST`

