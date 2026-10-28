# Bell and sound

Sound needs a DAC on the board (`BOARD_AUDIO_I2S_*` in its header: the
Frankless's TDA1387). The firmware plays a continuous 32 kHz stereo
stream to it. Other boards stay silent, and their sound settings show
`off (no DAC on this board)`.

## The bell

BEL (Ctrl-G) from the host or a CP/M program sounds a short beep: 100 ms
of 1 kHz; a new BEL while one sounds adds nothing. Ctrl-G at a CP/M prompt
is no test (CP/M echoes it as `^G`); F83's `7 EMIT` or a host's
`printf '\a'` sends a real BEL.

## The sound chip

The Z80 machine has a General Instrument AY-3-8910: three square-wave
tones, noise and an envelope, emulated, clocked at 1.8432 MHz, on RC2014's
ports. A tone of f Hz has period 115200 / f. Programs written for RC2014's
YM2149 sound board use the same ports; see
[Z80 I/O ports](../reference/ports.md#the-ay-3-8910-d8h-d0h).

## Settings

In SET-UP's Clock and sound panel (**C**):

| Setting | Choices | |
|---|---|---|
| Bell | off, 1-10 | how loud BEL beeps, 2 dB a step (default 3, safe in headphones); a beep sounds at each new level |
| Sound chip | off, 1-10 | the AY's level, 2 dB a step (default 7; at 10, three channels at full volume just reach the DAC's full scale); a chime sounds at each new level |
| Stereo | mono, ABC, ACB | where the AY's channels are heard: all in both ears; A left, B center, C right (as on the ZX Spectrum 128 and Amstrad CPC); or A left, C center, B right. Mono by default: a channel in one ear only tires on headphones |
| Sound DAC | fitted, not fitted | not fitted: no sound at all (the DAC's pins, PIO and DMA are given back), and the AY's ports read FFh as with no chip |

A program can pick the stereo layout itself through register 10h, which the
firmware adds (a real AY ignores it): see
[Z80 I/O ports](../reference/ports.md#the-ay-3-8910-d8h-d0h).

## Programs

- [AYTEST](../cpm/tools/aytest.md): a menu of the chip's parts.
- [AYPLAY](../cpm/tools/ayplay.md): plays PSG files; three tunes come with it.
- [AYTRACK](../cpm/tools/aytrack.md): a tracker to write tunes with.
- [F83 Forth](../cpm/f83.md): `AY.BLK` has words for the chip.
- SET-UP's **T** demo plays Korobeiniki on it.
