# Running the ROMs: emulator setup without lag or crackle

Audio is the whole point of these ROMs, so set the emulator up for sound first.
Two rules cover most problems:

1. **One clock in charge.** An emulator can pace itself by the audio device
   (audio "blocking"/sync) or by the screen (video vsync). Both at once fight
   each other: stutter and crackle. For these audio ROMs, let **audio** lead.
2. **Latency is a trade.** A smaller audio buffer means less delay between a
   fader move and the sound, but crackles as soon as the computer is busy.
   Start in the middle, go down until it crackles, then step back up.

What is marked **(here)** is what runs on our machine (an Intel Mac); the rest
is general advice, not tested here.

## Nintendo 64 - ares

[ares](https://ares-emu.net) v148 **(here)**. (Mupen64Plus showed a black screen for
every ROM on our Mac; ares runs them at full speed, 59 VPS.)

`Settings` in ares, the values we use **(here)**:

| setting | value | why |
|---|---|---|
| Video > Driver | Metal (macOS) | the fastest backend on a Mac |
| Video > Blocking (vsync) | off | audio leads (rule 1) |
| Video > Threaded renderer | on | keeps drawing off the emulation thread |
| Audio > Driver | SDL | |
| Audio > Frequency | 48000 Hz | the ROM's 22050 Hz is resampled to this |
| Audio > Latency | 20 ms | raise to 40-60 if it crackles |
| Audio > Blocking | **on** | the audio device paces the emulator: no drift, no crackle |
| Audio > Dynamic rate control | off | only needed when VIDEO leads |
| General > Run-ahead | off | cuts input lag in games; costs CPU, nothing for audio |

For **games** (smooth picture first) swap the leader: Video blocking on, Audio
blocking off, Dynamic rate control on - the emulator then stretches the audio
very slightly to follow the screen.

**The ROM has its own buffer too.** `faust2n64 -b N` sets how many audio buffers
the ROM queues; at 22050 Hz one buffer is 880 frames, about 40 ms **(here,
measured)**. The default `-b 4` is therefore about 160 ms before a fader move is
heard. For playing live, build with `-b 2` or `-b 3` (80-120 ms); it only
underruns when the DSP is close to 100% CPU (the demos use about 15%).

Real hardware: a flash cart (e.g. SummerCart64, EverDrive 64) takes the `.z64`
as it is - not tested here yet.

## GameCube / Wii - Dolphin

[Dolphin](https://dolphin-emu.org) 2606a **(here)**: `tools/play.sh file.dol`, or open
the `.dol` in Dolphin. **(here)** only one audio setting differs from the
defaults: `DSPThread = True` in `Dolphin.ini` (Config > Audio, DSP on its own
thread).

General advice:

| setting | value | why |
|---|---|---|
| Config > Audio > DSP emulation | HLE | fast; these ROMs do not need LLE accuracy |
| Config > Audio > backend | Cubeb | the low-latency one on macOS |
| Config > Audio > latency | 20-40 ms | rule 2 |
| Config > Audio > audio stretching | off | it smooths slowdowns by stretching sound - wrong for music timing |
| Config > General > dual core | on | |
| Graphics > VSync | off | audio leads |

## ZX Spectrum

Not run in an emulator here. `faust2zx` is checked by `zx/simulate.py` instead
(correlation 0.996-0.998 against the Faust render), and makes
`<name>-preview.wav`, which is what the speaker plays. Any emulator that loads
`.tap` files works in principle (e.g. Fuse); it must emulate the beeper at full
rate - check its sound settings for "beeper" or "sound" quality. On a real
Spectrum: `python3 zx/tap2wav.py x.tap x.wav`, play it into EAR, `LOAD ""`.

## Game Boy

[SameBoy](https://sameboy.github.io) **(here)**: `tools/play.sh x.gb`, or open the
`.gb` in it. All the Game Boy ROMs here run in it; for the picture ROMs
(`faust2gb -v`) the timing margin was measured in it: the player's video copy
started 14 M-cycles earlier or 16 later than it does and still lost nothing;
18 earlier or 20 later lost bytes (see `gb/mkgbv.py`).

The sound has been checked by `gb/simulate.py` / `gb/simv.py` (a model of the
Game Boy CPU runs the ROM's real player and compares the sound with the Faust
render; timing exact), not yet by recording an emulator's output.

The sound uses the master volume register as a 3-bit sample output - a trick
that needs an emulator with an accurate sound chip. If a ROM is silent or
distorted in one emulator, try another (SameBoy and mGBA are known for accurate
Game Boy sound). The picture ROMs also need accurate screen timing; only SameBoy
has been tried. Audio setup as for ares above.

Real hardware: any flash cart with MBC5 support (e.g. EverDrive GB, EZ-Flash Junior).
