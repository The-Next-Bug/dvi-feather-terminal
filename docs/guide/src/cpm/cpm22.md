# CP/M 2.2

`CPM22` is a 62K CP/M 2.2: Digital Research's CCP and BDOS (relocated from
picosim's) with this project's BIOS (`src/machine/cpm/bios.asm`), which
knows eight drives, A-H, each any of the three image formats (it asks the
machine which, through [port 8](../reference/ports.md#drive-format-8)).

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

## Time

CP/M 2.2 has no clock commands, but programs can read the machine's clock
(ports 65/66). Set the time in SET-UP's
[Clock and sound panel](../terminal/setup.md#the-clock-and-sound-panel).
