#!/usr/bin/env python3
"""The user guide's tables generated from headers (scripts/docs.sh, bd 1ay).

Usage: gen_tables.py REPO OUTDIR [CC]
Writes OUTDIR/hwinfo-items.md (src/machine/machine.h's HWINFO items and
statuses) and OUTDIR/boards/<target>.md (each boards/*.h's pins and
settings, as the C preprocessor resolves them, with the headers the
board includes).
"""

import os
import re
import subprocess
import sys

DEFINE = re.compile(r"#define\s+(HWINFO_\w+)\s+(\d+)\s*(?://\s*(.*))?$")


def hwinfo(repo, out):
    items, statuses = [], []
    with open(os.path.join(repo, "src/machine/machine.h")) as f:
        for line in f:
            m = DEFINE.match(line.strip())
            if not m:
                continue
            name, value, note = m.group(1), int(m.group(2)), (m.group(3) or "").strip()
            note = re.sub(r"\s*\(planned: bd [^)]*\)", " (planned)", note)
            note = re.sub(r"\s*\(bd [^)]*\)", "", note)
            note = note.replace("<", "&lt;").replace(">", "&gt;")
            if name.startswith("HWINFO_ST_"):
                statuses.append((value, name, note))
            elif name not in ("HWINFO_VERSION", "HWINFO_MAX", "HWINFO_RECORD"):
                items.append((value, name, note))
    with open(os.path.join(out, "hwinfo-items.md"), "w") as f:
        # Hex, as the Z80 I/O ports page writes ports and values; items
        # with their decimal numbers too (HWINFO's sources use them).
        f.write("| Item | Name | What |\n|---|---|---|\n")
        for value, name, note in sorted(items):
            f.write(f"| {value:02X}h ({value}) | `{name}` | {note} |\n")
        f.write("\n| Status | Name | Meaning |\n|---|---|---|\n")
        for value, name, note in sorted(statuses):
            f.write(f"| {value:02X}h | `{name}` | {note} |\n")


def board_macros(repo, header, cc):
    """BOARD_* and PICO_* macros of a board header, as the compiler sees them."""
    args = [cc, "-dM", "-E", "-x", "c", "-",
            "-I", os.path.join(repo, "boards"),
            "-I", os.path.join(repo, "lib/pico-sdk/src/boards/include"),
            "-D", "pico_board_cmake_set(a,b)=", "-D", "pico_board_cmake_set_default(a,b)="]
    src = f'#include "{header}"\n'
    out = subprocess.run(args, input=src, capture_output=True, text=True, check=True).stdout
    macros = {}
    for line in out.splitlines():
        parts = line.split(None, 2)
        if len(parts) == 3 and (parts[1].startswith("BOARD_") or parts[1].startswith("PICO_DEFAULT_LED")):
            macros[parts[1]] = parts[2]
    return macros


def gp(m, key):
    return f"GP{m[key]}" if key in m else None


def board_rows(m):
    rows = []
    dvi = ", ".join(f"{lane} {gp(m, f'BOARD_DVI_{lane}_P_PIN')}/{gp(m, f'BOARD_DVI_{lane}_N_PIN')}"
                    for lane in ("CLK", "D0", "D1", "D2"))
    rows.append(("DVI (HSTX, + / -)", dvi))
    uart = f"UART{m.get('BOARD_UART')}: TX {gp(m, 'BOARD_UART_TX_PIN')}, RX {gp(m, 'BOARD_UART_RX_PIN')}"
    if "BOARD_UART_CTS_PIN" in m:
        uart += f", CTS {gp(m, 'BOARD_UART_CTS_PIN')}, RTS {gp(m, 'BOARD_UART_RTS_PIN')}"
    else:
        uart += " (no CTS/RTS: Flow control offers XON/XOFF only)"
    rows.append(("Serial port", uart))
    rows.append(("SD card", f"SPI{m.get('BOARD_SD_SPI')}: MISO {gp(m, 'BOARD_SD_MISO_PIN')}, CS "
                 f"{gp(m, 'BOARD_SD_CS_PIN')}, SCK {gp(m, 'BOARD_SD_SCK_PIN')}, MOSI {gp(m, 'BOARD_SD_MOSI_PIN')}"))
    if "PICO_DEFAULT_LED_PIN" in m:
        rows.append(("Status LED", gp(m, "PICO_DEFAULT_LED_PIN")))
    if "BOARD_PSRAM_CS_PIN" in m:
        rows.append(("PSRAM", f"chip select {gp(m, 'BOARD_PSRAM_CS_PIN')}"))
    for chip in ("DS3231", "PCF8523"):
        if f"BOARD_RTC_{chip}" in m:
            rtc = f"{chip} on I2C{m.get('BOARD_RTC_I2C')}: SDA {gp(m, 'BOARD_RTC_SDA_PIN')}, SCL {gp(m, 'BOARD_RTC_SCL_PIN')}"
            if "BOARD_RTC_SQW_PIN" in m:
                rtc += f", 1 Hz on {gp(m, 'BOARD_RTC_SQW_PIN')}"
            rows.append(("Real-time clock", rtc))
    if "BOARD_AUDIO_I2S_BCK_PIN" in m:
        rows.append(("Audio DAC (I2S)", f"BCK {gp(m, 'BOARD_AUDIO_I2S_BCK_PIN')}, WS "
                     f"{gp(m, 'BOARD_AUDIO_I2S_WS_PIN')}, DATA {gp(m, 'BOARD_AUDIO_I2S_DATA_PIN')}"))
    if "BOARD_USB_VBUS_EN_PIN" in m:
        rows.append(("USB 5 V switch", gp(m, "BOARD_USB_VBUS_EN_PIN")))
    sys_khz = int(m.get("BOARD_SYS_KHZ", "384000"))
    rows.append(("CPU clock (clk_sys)", f"{sys_khz // 1000} MHz"))
    return rows


def boards(repo, out, cc):
    os.makedirs(os.path.join(out, "boards"), exist_ok=True)
    for name in sorted(os.listdir(os.path.join(repo, "boards"))):
        if not name.endswith(".h") or name == "example.h":
            continue
        target = name[:-2]
        m = board_macros(repo, name, cc)
        with open(os.path.join(out, "boards", target + ".md"), "w") as f:
            f.write("| | |\n|---|---|\n")
            for key, value in board_rows(m):
                f.write(f"| {key} | {value} |\n")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    repo, out = sys.argv[1], sys.argv[2]
    cc = sys.argv[3] if len(sys.argv) > 3 else "cc"
    os.makedirs(out, exist_ok=True)
    hwinfo(repo, out)
    boards(repo, out, cc)


if __name__ == "__main__":
    main()
