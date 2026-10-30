# Idea: video modes an HDMI capture device accepts (480p, 576p)

Status: speculation, not planned, no bd ticket. Written 2026-10-30 from a
read of the current video code (v0.11.6 + main); nothing has been built or
tried.

Capture devices often take only the CEA-861 formats: 720x480p60,
720x576p50, 1280x720p50/60, 1920x1080, up to 3840x2160p60. Neither of our
modes is one of them: 1024x600 is CVT reduced blanking, 800x600 is VESA.
To record the board's output directly, the firmware would need a CEA mode
of its own.

## Verdict

- **720x480p59.94 and 720x576p50: feasible**, and easier on the hardware
  than what we run now.
- **1280x720 and up: not realistic.** Worth at most a spike that is
  expected to fail.

## Why 480p and 576p fit

Both are 27.000 MHz pixel clocks. Our clocks (bd cpx.1, cpx.4) take the
pixel clock as PLL_SYS / 10 and clk_hstx as PLL_SYS / 2:

| | 1024x600 (now) | 800x600 (now) | 720x480p59.94 | 720x576p50 |
|---|---|---|---|---|
| Total | 1184 x 619 | VESA's | 858 x 525 | 864 x 625 |
| Pixel clock | 44 MHz | 40 MHz | 27 MHz | 27 MHz |
| PLL_SYS | 440 MHz | 400 MHz | 270 MHz | 270 MHz |
| clk_hstx | 220 MHz | 200 MHz | 135 MHz | 135 MHz |
| Line time | 26.9 us | 26.4 us | 31.8 us | 32.0 us |
| Syncs | +h, -v | -h, -v | -h, -v | -h, -v |

- PLL_SYS 270 MHz comes exactly from the 12 MHz crystal (VCO 1080 MHz,
  post dividers 4 x 1).
- clk_hstx 135 MHz is **inside** the RP2350's 150 MHz rating. Our current
  modes run 200-220 MHz (dmt.8.1).
- clk_sys stays on PLL_USB (384 MHz, the Feathers 336): the CPU, USB and
  the Z80 are untouched.
- Core 1 composes 720 pixels in 31.8 us instead of 1024 in 26.9 us. In
  1024x600 it is at ~49% of a line, peaking at 59% with 132 columns, so
  the new modes have more headroom, not less.
- `hstx_dvi.c` keeps timings and sync polarity per entry of `modes[]`, so
  each mode is mostly a new table entry.

## Why not 1280x720

720p's pixel clock is 74.25 MHz (720p30 too: it only widens the line).
That is clk_hstx at ~371 MHz, about 2.5 x the HSTX rating and well past
the 220 MHz we already push, and PLL_SYS at 742.5 MHz. Core 1 would also
need roughly 75-90% of each line (1280 pixels at 74.25 MHz against 1024
at 44 MHz). Line doubling or a smaller active area would not help: the
link clock is fixed by the format.

## What it would cost

1. **Fonts.** 80 columns in 720 pixels is 9-pixel cells, and Terminus has
   no 9-wide size (6x12, 8x14, 8x16, 10x18, 10x20, 11x22, 12x24, ...).
   Use Terminus 8x16, normal and bold, in a 9-pixel cell with the 9th
   column extended for line drawing (VGA style; `font_gen.py --pad-x`
   already does this for the 132-column font). Rows: 480 / 25 = 19
   scanlines (16 + padding), 576 / 25 = 23.
2. **132 columns.** 720 / 132 = 5.45 pixels a cell: it would need a 5-wide
   font (misc-fixed 5x8, doubled vertically), barely readable. Better:
   these modes are 80 columns only, Columns fixed in SET-UP the way the
   ADM-3A fixes it (`settings_field_fixed`, dmt.39).
3. **The Display setting** (field 21, bd cpx.6). Value 2 is already
   `SETTINGS_DISPLAY_NONE`, display_was's "no change waiting" (field 22),
   so the new modes are 3 (480p) and 4 (576p); `SETTINGS_DISPLAY_WIDTH`
   and the settings validation follow. The existing 15-second confirm
   (`display_confirm`) covers a mode the monitor will not show.
4. **Code that knows two widths.** About 14 places use `raster_width()` or
   `RASTER_WIDTH_800/1024`; the raster layouts, the inlined compose loops
   and the splash offset need a third case. SRAM should not grow: the
   buffers are sized for 1024.
5. **The logo.** `ColorLogo-576.png` is 576 x 576: it fills 576p's height
   exactly (the status row would overlap or be dropped) and does not fit
   480p at all. A smaller variant (e.g. 448 tall) for these modes, or no
   logo in them.
6. **Capture-device quirks.**
   - We send plain DVI (no HDMI data islands). HDMI sinks must accept it
     and most capture devices do, but check the actual device.
   - Without an AVI InfoFrame a sink may take a CEA format as
     limited-range RGB (16-235): blacks crushed, whites clipped. Fine for
     text, not exact.
   - 720-wide pixels in these formats are not square: the device will
     likely stretch the picture to 4:3 (or 16:9).
   - Both are fixed by HDMI data islands carrying an AVI InfoFrame (full
     range RGB, picture aspect). HSTX can send them as raw TERC4 symbols
     in the blanking command lists; other RP2350 projects do. Separate,
     optional work; it would also open the way to HDMI audio.

## Rough plan, if it is ever done

1. **Spike on hstx_test.** Add 720x480p59.94 and 720x576p50 to
   `apps/hstx_test` only. Check that the capture device locks, and how it
   treats range and aspect. Go / no-go; cheap.
2. **Video modes.** Timings in `modes[]` with PLL_SYS 270 MHz; Display
   values 3 and 4 with the confirm-or-revert, SET-UP's row, HWINFO's video
   record.
3. **Fonts and raster.** Terminus 8x16 (normal, bold) generated at build
   time; a 720-wide layout, 9x19 (480p) or 9x23 (576p); 132 columns fixed
   off in these modes ("80 (480p)" in SET-UP); `test_raster` and
   `test_splash` at the new widths.
4. **Logo, guide, screenshots.** A logo that fits or none; the guide's
   display page and docgen's screenshots.
5. **Optional: AVI InfoFrame** over HSTX data islands, only if step 1
   shows the device getting range or aspect wrong.
6. **Not planned: 720p.** A separate experiment if ever.

## A no-firmware alternative

An HDMI scaler box between the board and the capture device (1024x600 in,
1080p out). Many scalers do not take 1024x600 either, so it depends on the
box; try 800x600 too.
