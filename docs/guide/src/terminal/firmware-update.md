# Firmware updates

New firmware can be installed from an SD card or a USB stick, in either
mode, without a PC.

1. Copy the new firmware for your board to the root of the SD card or a
   USB stick as `FIRMWARE.UF2` (8.3 names only). The release ZIP has it as
   `sdcard-<target>/FIRMWARE.UF2`.
2. Put the card or stick in. The stick is the one the status line shows as
   `USB`.
3. Open SET-UP and press **U**. In terminal mode the card is mounted only
   for the update. With `FIRMWARE.UF2` on both, **S** or **U** picks which
   to install.
4. It reads the file through and checks it (a UF2 for the RP2350 with a
   bootable image that fits), shows its size and how long reading took, and
   asks. **Y** then:
   1. writes the image to the upper half of the flash, beside the running
      firmware, and reads it back (the progress bar). Any problem up to
      here leaves the board as it was;
   2. renames the file to `FIRMWARE.OLD` on the card or stick it came from,
      so it is not installed twice, and closes the disks;
   3. copies the image over the running firmware (a few seconds; the screen
      says not to power off) and restarts with it.

The settings are kept.

A full power cycle after an update (unplug the board, plug it back in) is
recommended: the USB host (keyboard, sticks) has been seen not working
after the update's restart until the power was cycled. The panel says so
while it installs.

If the power fails during step 3 the board will not start: flash it over
USB with BOOTSEL (see [Flashing](../start/flashing.md)), which always works.
