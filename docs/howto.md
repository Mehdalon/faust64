# Step by step: run the examples, then make your own

Commands are run from the top folder of this repository.

## 1. What you need

| for | install | check |
|---|---|---|
| everything | [Faust](https://faust.grame.fr/downloads/) 2.x (the C backend), `python3`, a C compiler (`cc`), `bash` | `faust --version`, `cc --version` |
| Nintendo 64 | the [libdragon](https://github.com/DragonMinded/libdragon) toolchain; `faust2n64` looks in `$N64_INST` (default `~/n64-toolchain`) | `ls $N64_INST/include/n64.mk` |
| GameCube / Wii | [devkitPro](https://devkitpro.org/wiki/Getting_Started) with devkitPPC and libogc; `faust2gc` looks in `$DEVKITPRO` (default `/opt/devkitpro`) | `ls /opt/devkitpro/devkitPPC` |
| MS-DOS | the [DJGPP](https://www.delorie.com/djgpp/) cross-compiler (`i586-pc-msdosdjgpp-gcc`; prebuilt for macOS/Linux: [build-djgpp](https://github.com/andrewwutw/build-djgpp/releases)); `faust2dos` looks on the PATH, then in `$DJGPP_PREFIX` (default `~/djgpp`) | `i586-pc-msdosdjgpp-gcc --version` |
| ZX Spectrum, Game Boy | nothing more: the sound is rendered on your computer | |
| playing the ROMs | an emulator per console: [ares](https://ares-emu.net) (N64), [Dolphin](https://dolphin-emu.org) (GameCube/Wii), [SameBoy](https://sameboy.github.io) (Game Boy), [Fuse](https://fuse-emulator.sourceforge.net) or Spectral (ZX), [DOSBox Staging](https://www.dosbox-staging.org) (MS-DOS) | |

If `faust` is not on your PATH, or you want a particular build, point to it:
`export FAUST=/path/to/faust`. Every script here uses `$FAUST` when it is set.

Any recent Faust runs the examples. Our fork fixes two compiler bugs your own
programs may hit (an int and a float squared in one program; fixed-point tables):
[faust-version.md](faust-version.md) says when you need it and how to build it.

## 2. Try the ready-made ROMs

`roms/` has ROMs built from the files in this repository. Open them in the emulator
(first set the emulator up as in [emulators.md](emulators.md) - it decides
whether you hear lag and crackle):

| file | console | what it is |
|---|---|---|
| `roms/acid.z64` | N64 | a self-playing 303 line; every control of the program is a fader on screen, chosen and moved with the d-pad |
| `roms/hardkick.z64`, `roms/arp.z64` | N64 | a hardstyle kick; a chip arpeggio |
| `roms/demo.z64` | N64 | `game/faust64.h` in a small game: Faust music, and Faust sound effects on A (jump), B, START, Z, L, R; the d-pad moves |
| `roms/chip.gb` | Game Boy | a two-voice tune written for the Game Boy's 3-bit output - no added noise |
| `roms/acid.gb`, `roms/hardkick.gb` | Game Boy | the N64 demos at 8192 Hz, 3-bit: you hear the dither noise |
| `roms/acid.exe`, `roms/hardkick.exe`, `roms/chip.exe` | MS-DOS | the same programs live on a PC with a Sound Blaster 16; the faders on a VGA screen (arrow keys, Space, R resets, Esc quits) |
| `roms/gb-tunnel.gb`, `roms/gb-plasma.gb` | Game Boy | **pictures and sound, both from Faust**: a tunnel / a plasma drawn by a Faust program, the tune from another, and the picture rippling on every beat |

`tools/play.sh FILE` starts the right emulator on macOS (ares, Dolphin, SameBoy,
Spectral, DOSBox Staging) with the settings in emulators.md; set `ARES=`,
`DOLPHIN=`, `SAMEBOY=`, `DOSBOX=` to other paths.

## 3. Hear a program before building anything

```bash
lab/faustlab demos/acid.dsp                      # renders 8 s at the N64 rate and plays it (macOS: afplay)
lab/faustlab -l demos/acid.dsp                   # list its controls
lab/faustlab -p cutoff=900 -d 12 demos/acid.dsp  # set a control, longer render
lab/faustlab -n -o acid.wav demos/acid.dsp       # just write the WAV (use -n where there is no afplay, e.g. Linux)
python3 lab/labserver.py                         # the same in a browser: http://127.0.0.1:8791/
```

`-r 8192` renders at the Game Boy rate, `-r 6986` at the ZX rate: what you hear
is what those consoles get, before the 3- or 5-bit step.

## 4. Build the examples

```bash
./faust2n64 demos/acid.dsp      # -> demos/acid.z64
./faust2gc  demos/acid.dsp      # -> demos/acid.dol          (--wii for a Wii build)
./faust2dos demos/acid.dsp      # -> demos/acid.exe          (MS-DOS; -r 11025 for slow PCs)
./faust2zx  demos/acid.dsp      # -> demos/acid.tap + demos/acid-preview.wav
./faust2gb  demos/chip.dsp      # -> demos/chip.gb  + demos/chip-preview.wav
./faust2gb -d 20 -v gb/vis/tunnel.dsp gb/vis/chipwave.dsp   # -> gb/vis/chipwave.gb, sound + pictures
```

What each prints, and what to look for:

- **faust2n64 / faust2gc** end with `wrote demos/acid.z64 (... bytes)`. Every
  `hslider`/`vslider`/`button`/`checkbox` in the program becomes a control on screen.
  `faust2n64 -b 2` makes the sound react faster to the controls (see emulators.md).
- **faust2dos** ends with `wrote demos/acid.exe (... bytes, 22050 Hz)`. One file:
  the DOS extender's helper (CWSDPMI) is built in. DOS names have at most 8
  characters, so `frenchcore.dsp` becomes `frenchco.exe`. `-r 11025` halves the
  CPU cost for slow PCs; `-b` sets the buffer (default 1024 frames per half,
  46 ms). `faust2dos -S x.dsp` builds a self-test that prints a fingerprint of
  the first 4096 samples, to compare with `tools/selftest.sh x.dsp` on your computer.
  In DOS, `set FAUSTDOS_TEST=5` before starting a program makes it play 5 s and
  write `TEST.TXT` (measured sample rate, CPU load, late buffers) and `SCREEN.PPM`.
- **faust2zx / faust2gb** render on your computer, then run the ROM's player on a
  model of the CPU (`zx/simulate.py`, `gb/simulate.py`) and print how close the
  console's sound is to the render: `correlation` (1 = identical) and `SNR` (dB,
  higher is cleaner). `demos/chip.dsp` gives 1.0000 / 98 dB; noisy, quiet
  programs land near 0.6 / 2 dB - see section 5. The `-preview.wav` is what the
  console will play.
- **faust2gb -v** prints three checks from `gb/simv.py`, which runs the ROM on a
  model of the Game Boy CPU and screen: the sound intervals (`[114] M-cycles`) and
  `sound = encoded stream`; `lost in mode 3: 0` (no picture byte was written while
  the screen was reading); `lines differing from the Faust picture: 0`. It also
  writes `...-frames.png`: four screens as the Game Boy draws them.

To build all the demos for one console: `for f in demos/*.dsp; do ./faust2n64 "$f"; done`.

## 5. Make your own sound program

Any Faust program with 0 inputs and 1 or 2 outputs works. Start from a copy:

```bash
cp demos/chip.dsp demos/mine.dsp
lab/faustlab demos/mine.dsp          # listen, change, listen
./faust2n64 demos/mine.dsp           # then build for the consoles you want
./faust2gb  demos/mine.dsp
```

The smallest one:

```faust
import("stdfaust.lib");
freq = hslider("freq", 220, 50, 1000, 1);     // becomes a fader on the N64
process = os.osc(freq) * 0.5 <: _, _;
```

Rules that matter on these consoles:

- **Use `ma.SR` for anything that depends on time** (pitches, step lengths,
  envelopes): the same program then runs at 22050 Hz (N64), 48000 (GameCube),
  8192 or 9198 (Game Boy), 6986 (ZX).
- **Keep filter frequencies below half the sample rate.** At 8192 Hz a
  highpass at 5500 Hz is above the 4096 Hz limit and the filter blows up. The demos
  use `nyq(f) = min(f, 0.45 * ma.SR);` around every filter frequency.
- **Game Boy and ZX have very few levels** (8 and 32). Loud, simple sounds
  survive; quiet noise does not. Best of all on the Game Boy: write the output
  directly on its 8 levels, as `demos/chip.dsp` does (two pulse voices, 0-4 and
  0-3, summed, mapped to -1..1) - `faust2gb` then passes it through exactly,
  with no dither noise at all.
- **N64 cost:** the screen shows the DSP's share of the CPU; near 100% the
  sound breaks up - simplify the program. `tools/bench.sh` measures programs.
- **Controls:** on the N64 and GameCube the program starts at the controls'
  default values; on ZX and Game Boy the render uses them (`-p name=value`
  changes them for the build).

## 6. Make Game Boy pictures with Faust

`faust2gb -v PICTURES.dsp SOUND.dsp` puts two Faust programs on one cartridge:

- **the picture program** runs one sample per pixel, row by row: 128 x 88 pixels,
  29.86 pictures a second. Its first output is the brightness, 0 (black) to 1
  (white). Values already on the 4 levels (0, 1/3, 2/3, 1) are kept exactly;
  anything else is dithered to the Game Boy's 4 shades. In the file, `@FW@`, `@FH@`
  and `@FPS@` are replaced by 128, 88 and 29.8638.
- **the sound program** runs one sample per screen line (9198 Hz). If it has the
  line `// faust2gb: scx`, its second output moves each screen line sideways
  (value x 128 = pixels): raster effects in time with the music.

Start from a copy of `gb/vis/plasma.dsp`:

```faust
import("stdfaust.lib");
import("vis.lib");                       // gb_xyt: pixel x, y and the time t in seconds
W = @FW@; H = @FH@; FPS = @FPS@;
process = gb_xyt(W, H, FPS) : pix;
pix(x, y, t) = 0.5 + 0.5 * sin(x * 0.1 + t * 2.0);   // moving vertical bars
```

```bash
./faust2gb -d 20 -v gb/vis/mine.dsp gb/vis/chipwave.dsp
tools/play.sh gb/vis/chipwave.gb
```

Use the same tempo in both programs to keep them in step (the demos use 132 bpm:
`t * bpm / 60` is the beat in the picture program). Limits: 128 x 88, 4 shades,
29.86 pictures a second; up to about 77 seconds in an 8 MB cartridge.

How it works, in short: the player is locked to the screen's line clock - per
line it writes one sound sample, copies 10 bytes of picture while the screen is
not reading video memory, and sets that line's scroll; each frame takes exactly
17556 cycles, so nothing drifts. Each picture is written over two frames into
the half of video memory not on screen, then shown. Details in `gb/mkgbv.py`.

## 7. Real hardware

- **N64:** a flash cart (SummerCart64, EverDrive 64) takes the `.z64` as it is.
- **MS-DOS:** copy the `.exe` to a PC with a Sound Blaster 16 (or compatible), `SET BLASTER=A220 I5 D1 H5 T6` as your card is set, run it. A 486DX at 66 MHz is about the minimum for most programs at 11025 Hz (estimated in DOSBox, see emulators.md).
- **GameCube / Wii:** a `.dol` via Swiss (GameCube) or the Homebrew Channel (Wii).
- **ZX Spectrum:** `python3 zx/tap2wav.py demos/acid.tap acid-tape.wav`, play it into the EAR socket, `LOAD ""`.
- **Game Boy:** any flash cart with MBC5 support (EverDrive GB, EZ-Flash Junior).

None of these has been tested on real hardware yet. If you try one, open an issue
with what happened.

## 8. When something fails

| message | fix |
|---|---|
| `faust compiler not found` | install Faust, or `export FAUST=/path/to/faust` |
| `no libdragon toolchain at N64_INST=...` | install libdragon, `export N64_INST=/its/folder` |
| `no devkitPPC at ...` | install devkitPro's `gamecube-dev` (or `wii-dev`), or set `DEVKITPRO` |
| `no DJGPP compiler` | install DJGPP, put its `bin` on PATH or set `DJGPP_PREFIX` |
| DOS: `no Sound Blaster at 220h` | in DOSBox set `sbtype=sb16`; on a PC set `BLASTER` to your card's settings |
| DOS: `Bad command or file name` for a long name | DOS names are 8 characters: `frenchcore.dsp` is `frenchco.exe` |
| a Game Boy / ZX build sounds like noise | the program is quiet or full of noise - see the rules in section 5; compare with `lab/faustlab -r 8192` |
| `correlation` well below 0.9 | the same: the console's few levels cannot carry this sound |
| clicks or stutter in an emulator | [emulators.md](emulators.md): let the audio pace the emulator |
