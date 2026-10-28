# Clock

## Where the time comes from

- **Boards with a real-time clock chip**: the Frankless's DS3231M (with a
  cell in its battery holder) and the Adalogger FeatherWing's PCF8523 (with
  a CR1220). It is read at power-on and kept by the firmware from there.
  On the Frankless the chip's 1 Hz output paces it a second at a time, so
  it keeps the chip's time instead of drifting; the Adalogger's 1 Hz output
  is not wired to the Feather, so there the firmware's timer keeps it until
  the next power-on. The PCF8523 is the less exact chip (about 2 seconds a
  day off, either way) and keeps the years to 2099.
- **Without a chip**, or while its time is not set, the clock starts at
  1 Jan 2026 at power-on.

## On the status line

Once the time is real (read from the chip, or set since power-on), the
status line ends in it, `14:05` by default, the colon blinking in step with
the second. SET-UP's Clock and sound panel (**C**) chooses the format:

| Setting | Choices |
|---|---|
| Status line clock | 24 h, 12 h, off |
| Seconds | off, on |
| Date | off, `2026-10-22`, `Thu 22 Oct` |

At its widest (`Thu 22 Oct  1:45:30 PM`) the clock takes 24 columns, and
the end of the terminal's status line gives way to it.

## Setting it

In the Clock and sound panel, type the date and time over the Date and
Time rows (Left/Right move the cursor, Backspace goes back) and press
Return: the clock is set, and the chip with it, so it survives power-off.
Without a chip the panel says the time lasts only until power-off.

Under CP/M 3, `DATE` shows the time and `DATE SET` sets it, the chip's
too. Programs read it from the machine's clock ports (41h/42h; see
[Z80 I/O ports](../reference/ports.md#clock-41h-42h)).

Files written on the SD card and USB sticks (`XPIP SD:...`, `DRIVES.CFG`,
new disk images, `USBLOG.TXT`) get their dates from it.

[HWINFO](../cpm/tools/hwinfo.md) shows which chip it is, whether it
answered, whether its time was set, whether its 1 Hz output ticks and, for
the PCF8523, whether its battery is low.
