# FAQ

**My monitor shows nothing.**
Wait out the 10-second logo; check the monitor takes 1024x600 at 60 Hz, or
switch to 800x600 in SET-UP (Display). See
[Troubleshooting](../terminal/troubleshooting.md#no-picture).

**Which TERM should the host use?**
`xterm-256color` for colors and the alternate screen, `vt100` for plain
VT100 software. See [Connecting a host](../start/host.md#line-settings).

**Characters go missing during fast output.**
Turn on [flow control](../start/host.md#flow-control), RTS/CTS if the board
and adapter have the wires, otherwise XON/XOFF.

**Can I use two hubs?**
Yes: up to three, the Frankless's own included (two have been tried; a
third is experimental). See
[Keyboard](../terminal/keyboard.md).

**My USB stick is not seen.**
It must be FAT12/16/32 with 512-byte sectors; exFAT sticks need formatting
as FAT32. See [SD card and USB sticks](../cpm/storage.md#usb-sticks).

**How do I get files onto CP/M disks?**
Put them in `/XFER` on the SD card or a USB stick and use `XPIP`; see
[Moving files](../cpm/moving-files.md).

**CP/M 2.2 does not see the disk I just mounted.**
Type ^C at the prompt. See [Drives](../cpm/drives.md#mounting).

**How fast is the Z80?**
It runs unthrottled: roughly 40-50 MHz of Z80 time on a Pico 2. The
monitor's **S** measures it; the status line shows it while CP/M runs.

**Are my settings kept when I update the firmware?**
Yes, both over USB and from the SD card.

**The board seems hung.**
SET-UP's **R** restarts it. A crash shows `PANIC`, `HARD FAULT` or `STUCK`
on the status line and the board restarts by itself after 20 seconds; see
[Troubleshooting](../terminal/troubleshooting.md#crash-reports).
