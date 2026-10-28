# HWINFO

```
HWINFO
```

Shows the firmware, the board, and a line for each piece of hardware the
board has, a screen at a time (`-- More --`: any key for the next page,
`Q`, `Esc` or `^C` to stop). Each line says whether the hardware is in use:

| Status | Meaning |
|---|---|
| `yes` | there and in use |
| `no` | supported, but nothing is plugged in, or this build does not use it |
| `missing` | the board should have it, but it did not answer |
| `planned` | the board header names it, but the firmware does not drive it yet |

What a board does not have is left out (its header, `boards/<target>.h`,
says what it has). On a Pico 2 with the DVI Sock, in CP/M mode:

```
Firmware         DVI Feather Terminal v0.11.0 (2026-10-28)
Board            pico2-dvi-sock

CPU              yes      RP2350A at 384 MHz, 4 MB flash, quad reads at 50 MHz
Z80 memory       yes      64 KB + 2 x 48 KB banks in SRAM
Video            yes      DVI 1024x600 at 60 Hz (reduced blanking), 80 x 24 text + status line
Serial port      yes      UART0 TX GP0 / RX GP1, 115200 8N1 (SIO2)
Flow control     no       off (SET-UP: RTS/CTS or XON/XOFF)
SD card          yes      SDHC/SDXC, 29.7 GB; SPI0: GP4 MISO, GP5 CS, GP6 SCK,
                          GP7 MOSI
USB host         yes      native USB port, 1 keyboard
USB stick        no       none
Status LED       yes      GP25: disks, serial, heartbeat
Disk drives      yes      A: CPM22, B: Z80TESTS
```

It reads them from the [hardware information device](../../reference/hwinfo.md)
on port 12, which other programs can use too. On older firmware or on
z80pack's picosim, which do not have the device, it says there is no
information.
