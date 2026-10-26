#!/usr/bin/env python3
"""The AYPLAY example tunes (bd 1ls.18.10) as PSG files.

Our own arrangements of public-domain melodies, written as note lists
and rendered here into PSG: the AY-3-8910's register writes, frame by
frame at 50 Hz, as AYPLAY.COM plays them.

  psg_gen.py OUTDIR

writes OUTDIR/<name>.psg for each tune in TUNES.

PSG: "PSG" 1Ah, a version byte, the frame rate (0: 50 Hz) and 10 zeros;
then per frame its register writes (register 0-15, value), FFh ending the
frame; FEh n waits n x 4 frames, FDh ends the tune. Periods are for the
1.8432 MHz clock of RC2014's sound board and our Z80 machine.
"""
import math
import sys

CLOCK = 1843200
FRAME_HZ = 50
NAMES = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7,
         "G#": 8, "A": 9, "A#": 10, "B": 11}


def period(note):
    """'E5', 'F#3' -> the tone period: 1.8432 MHz / (16 f)."""
    name, octave = note[:-1], int(note[-1])
    n = octave * 12 + NAMES[name]
    hz = 440.0 * 2 ** ((n - 57) / 12)  # 57 = A4
    return round(CLOCK / (16 * hz))


def notes(text):
    """'E5:2 B4:1 r:2' -> [('E5', 2), ('B4', 1), (None, 2)]: lengths in steps."""
    out = []
    for item in text.split():
        note, steps = item.split(":")
        out.append((None if note == "r" else note, int(steps)))
    return out


class Voice:
    """One channel: notes (or drum hits) with a volume envelope per frame.

    level: the attack's volume (0-15); decay: volume lost per frame down to
    sustain; the note's last frame is silent when gap (so repeated notes
    are heard as two). noise: drum hits instead of tones, each note a noise
    period ('n12:1')."""

    def __init__(self, text, level, decay, sustain, gap=True, noise=False, start=0):
        self.events = notes(text) if not noise else [
            (None if n == "r" else int(n[1:]), int(s)) for n, s in
            (item.split(":") for item in text.split())]
        self.level, self.decay, self.sustain = level, decay, sustain
        self.gap, self.noise, self.start = gap, noise, start

    def frames(self, step):
        """Per frame: (period or noise period or None, volume)."""
        out = [(None, 0)] * (self.start * step)
        for note, steps in self.events:
            length = steps * step
            for f in range(length):
                if note is None or (self.gap and f == length - 1):
                    out.append((None, 0))
                else:
                    vol = max(self.sustain, self.level - int(f * self.decay))
                    out.append((note if self.noise else period(note), vol))
        return out


def render(voices, step):
    """Voices on A, B, C (None: unused) -> PSG bytes."""
    tracks = [v.frames(step) if v else [] for v in voices]
    length = max(len(t) for t in tracks)
    data = bytearray(b"PSG\x1a" + bytes(12))
    regs = {}
    for f in range(length + 1):  # and a silent frame at the end
        want = {}
        tone_off, noise_off = 0, 0
        noise_period = regs.get(6, 0)
        for ch, track in enumerate(tracks):
            p, vol = track[f] if f < len(track) else (None, 0)
            is_noise = voices[ch] is not None and voices[ch].noise
            if p is not None and is_noise:
                noise_period = p
                tone_off |= 1 << ch
            elif p is not None:
                want[2 * ch] = p & 0xff
                want[2 * ch + 1] = p >> 8
                noise_off |= 1 << ch
            else:
                tone_off |= 1 << ch
                noise_off |= 1 << ch
                vol = 0
            want[8 + ch] = vol
        want[6] = noise_period
        want[7] = tone_off | noise_off << 3
        for reg in sorted(want):
            if regs.get(reg) != want[reg]:
                data += bytes([reg, want[reg]])
                regs[reg] = want[reg]
        data.append(0xFF)
    data.append(0xFD)
    return bytes(data)


def twice(text):
    return text + " " + text


# Korobeiniki (a 19th-century Russian folk song; the Tetris theme), as
# SET-UP's demo plays it (src/sound/tune.c), twice: the melody on A, an
# octave bass on B, a hi-hat on C each beat.
KOROBEINIKI_MELODY = """
E5:2 B4:1 C5:1 D5:2 C5:1 B4:1  A4:2 A4:1 C5:1 E5:2 D5:1 C5:1
B4:3 C5:1 D5:2 E5:2  C5:2 A4:2 A4:4
r:1 D5:2 F5:1 A5:2 G5:1 F5:1  E5:3 C5:1 E5:2 D5:1 C5:1
B4:2 B4:1 C5:1 D5:2 E5:2  C5:2 A4:2 A4:2 r:2
"""
KOROBEINIKI_BASS = " ".join(
    f"{lo}:1 {hi}:1 " * 4 for lo, hi in [
        ("E2", "E3"), ("A2", "A3"), ("G#2", "G#3"), ("A2", "A3"),
        ("D2", "D3"), ("C3", "C4"), ("G#2", "G#3"), ("A2", "A3")])

# Frere Jacques (a French round, 18th century) as a round in three
# voices, each entering two bars after the last: on ABC stereo it moves
# from left to center to right.
JACQUES = """
C:2 D:2 E:2 C:2 C:2 D:2 E:2 C:2
E:2 F:2 G:4 E:2 F:2 G:4
G:1 A:1 G:1 F:1 E:2 C:2 G:1 A:1 G:1 F:1 E:2 C:2
C:2 G-:2 C:4 C:2 G-:2 C:4
"""


def octave(text, n):
    """JACQUES' notes in octave n (G- one below)."""
    out = []
    for item in text.split():
        note, steps = item.split(":")
        if note.endswith("-"):
            out.append(f"{note[:-1]}{n - 1}:{steps}")
        else:
            out.append(f"{note}{n}:{steps}")
    return " ".join(out)


# Ode to Joy (Beethoven's 9th symphony, 1824): the melody on A, a bass in
# half notes on B, the chord's third on C.
ODE_MELODY = """
E5:2 E5:2 F5:2 G5:2  G5:2 F5:2 E5:2 D5:2  C5:2 C5:2 D5:2 E5:2  E5:3 D5:1 D5:4
E5:2 E5:2 F5:2 G5:2  G5:2 F5:2 E5:2 D5:2  C5:2 C5:2 D5:2 E5:2  D5:3 C5:1 C5:4
D5:2 D5:2 E5:2 C5:2  D5:2 E5:1 F5:1 E5:2 C5:2  D5:2 E5:1 F5:1 E5:2 D5:2  C5:2 D5:2 G4:4
E5:2 E5:2 F5:2 G5:2  G5:2 F5:2 E5:2 D5:2  C5:2 C5:2 D5:2 E5:2  D5:3 C5:1 C5:4
"""
# Per bar: one chord (root, then fifth) or two (each root), in halves.
ODE_CHORDS = ["C", "G", "C", "G", "C", "G", "C", "GC",
              "G", "GC", "GC", "CG", "C", "G", "C", "GC"]
ROOT = {"C": "C3", "G": "G2"}
FIFTH = {"C": "G3", "G": "D3"}
THIRD = {"C": "E4", "G": "B3"}


def ode_parts():
    bass, alto = [], []
    for bar in ODE_CHORDS:
        if len(bar) == 1:
            bass += [f"{ROOT[bar]}:4", f"{FIFTH[bar]}:4"]
            alto += [f"{THIRD[bar]}:4", f"{THIRD[bar]}:4"]
        else:
            bass += [f"{ROOT[c]}:4" for c in bar]
            alto += [f"{THIRD[c]}:4" for c in bar]
    return " ".join(bass), " ".join(alto)


def tunes():
    ode_bass, ode_alto = ode_parts()
    return {
        # An eighth: 10 frames, 200 ms.
        "korobein": render([
            Voice(twice(KOROBEINIKI_MELODY), 13, 0.25, 9),
            Voice(twice(KOROBEINIKI_BASS), 11, 0.5, 7),
            Voice(" ".join(["n1:2"] * 64), 9, 3, 0, noise=True),
        ], 10),
        # An eighth: 8 frames; two rounds of the melody per voice.
        "jacques": render([
            Voice(twice(octave(JACQUES, 5)), 13, 0.2, 9),
            Voice(twice(octave(JACQUES, 4)), 13, 0.2, 9, start=16),
            Voice(twice(octave(JACQUES, 5)), 13, 0.2, 9, start=32),
        ], 8),
        # An eighth: 12 frames (a quarter 480 ms).
        "ode": render([
            Voice(ODE_MELODY, 13, 0.15, 10),
            Voice(ode_bass, 11, 0.1, 7, gap=False),
            Voice(ode_alto, 8, 0.05, 6),
        ], 12),
    }


def main():
    out = sys.argv[1]
    for name, data in tunes().items():
        with open(f"{out}/{name}.psg", "wb") as f:
            f.write(data)


if __name__ == "__main__":
    main()
