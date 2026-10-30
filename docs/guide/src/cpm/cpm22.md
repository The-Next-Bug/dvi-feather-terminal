# CP/M 2.2

`CPM22` is a 62K CP/M 2.2: Digital Research's CCP and BDOS (relocated from
picosim's) with this project's BIOS (`src/machine/cpm/bios.asm`), which
knows eight drives, A-H, each any of the three image formats (it asks the
machine which, through [port 08h](../reference/ports.md#drive-format-08h)).

## Booting

Mount `CPM22` on A: (the monitor's install does) and press Return in the
monitor, or reset with **F11**. Drive B: is usually `Z80TESTS`.

## Everyday use

- `DIR`, `STAT`, `TYPE`, `ERA`, `REN`, `PIP`, `ED`, `ASM`, `LOAD`, `DDT`,
  `DUMP`, `SUBMIT`, `XSUB` and `SYSGEN` are CP/M's own.
- After changing a disk on a drive, type **^C** at the prompt so CP/M logs
  it in again.
- Return sends CR alone; backspace sends BS by default (SET-UP's Backspace
  sends), which CP/M takes as a rub-out.
- This project's tools are on the disk too: see [Tools](tools/index.md)
  and [Moving files](moving-files.md).

## The serial port

The board's serial port is CP/M's reader and punch (RDR: and PUN:), at
SET-UP's line settings and flow control:

```
A>PIP PUN:=REPORT.TXT
A>PIP CAPTURE.TXT=RDR:
```

PIP reads from the reader until a ^Z (1Ah), so end what the other side
sends with one. Sending to the punch, PIP adds 40 NULs (00h) of paper-tape
leader before and after the file, and a ^Z at the end; CP/M 3's PIP
does not. The printer (LST:) is not connected. The BIOS ignores the
IOBYTE: RDR: and PUN: are always the serial port, CON: always the screen
and keyboard.

## Time

CP/M 2.2 has no clock commands, but programs can read the machine's clock
(ports 41h/42h). Set the time in SET-UP's
[Clock and sound panel](../terminal/setup.md#the-clock-and-sound-panel).
