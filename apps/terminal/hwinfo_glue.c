// The Z80 machine's hardware information device (port 12, machine.h; bd
// 1ls.9): the board and its hardware for HWINFO.COM, from the board header
// and what the firmware found. Planned hardware the firmware does not drive
// yet has a record too (HWINFO_ST_PLANNED). Cold code in flash (ld/): it
// runs only when a program asks.

#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/structs/sysinfo.h"

#include "board.h"
#include "extram.h"
#include "hstx_dvi.h"
#include "nvflash.h"
#include "sdcard.h"
#include "terminal.h"
#include "term_build.h"
#include "usbkbd.h"
#include "usbmsc.h"

#ifndef PSRAM_BANKS
#define PSRAM_BANKS 0
#endif
#ifndef VIDEO_DIAG
#define VIDEO_DIAG 0
#endif

// "GP<pin>", as text at compile time (board.h's pins are plain numbers).
#define GP(pin) "GP" BOARD_STR(pin)

static size_t cpu_record(uint8_t *buf) {
	char text[64];
	// SYSINFO's PACKAGE_SEL: 1 = QFN60 (RP2350A), 0 = QFN80 (RP2350B).
	snprintf(text, sizeof text, "RP2350%c at %lu MHz, %lu MB flash, %s reads at %lu MHz",
		sysinfo_hw->package_sel & 1 ? 'A' : 'B', (unsigned long)(clock_get_hz(clk_sys) / 1000000),
		(unsigned long)(PICO_FLASH_SIZE_BYTES >> 20), nvflash_quad() ? "quad" : "serial",
		(unsigned long)(nvflash_read_hz() / 1000000));
	return hwinfo_record(buf, HWINFO_ST_PRESENT, "CPU", text);
}

static size_t memory_record(uint8_t *buf) {
	char text[64];
	unsigned banks = machine_banks();
	uint8_t *bank1 = machine_bank(1);
	// The PSRAM is mapped at 0x11000000, above the flash; SRAM at 0x20000000.
	bool psram = bank1 && (uintptr_t)bank1 < 0x20000000u;
	snprintf(text, sizeof text, "64 KB + %u x 48 KB banks%s", banks,
		banks ? (psram ? " in PSRAM" : " in SRAM") : "");
	return hwinfo_record(buf, HWINFO_ST_PRESENT, "Z80 memory", text);
}

static size_t psram_record(uint8_t *buf) {
#ifdef BOARD_PSRAM_CS_PIN
	char text[64];
	if (!PSRAM_BANKS) {
		return hwinfo_record(buf, HWINFO_ST_PLANNED, "PSRAM",
			"CS1 on " GP(BOARD_PSRAM_CS_PIN) ", not used by this build");
	}
	size_t size = extram_size();
	if (!size) {
		return hwinfo_record(buf, HWINFO_ST_NOT_FOUND, "PSRAM", "none answered on CS1 (" GP(BOARD_PSRAM_CS_PIN) ")");
	}
	snprintf(text, sizeof text, "%u KB on CS1 (" GP(BOARD_PSRAM_CS_PIN) "), Z80 banks", (unsigned)(size / 1024));
	return hwinfo_record(buf, HWINFO_ST_PRESENT, "PSRAM", text);
#else
	return hwinfo_record(buf, HWINFO_ST_ABSENT, "PSRAM", "none on this board");
#endif
}

static size_t video_record(uint8_t *buf) {
	char text[64];
	unsigned hz = HSTX_MODE == 1 ? 56 : 60;
	snprintf(text, sizeof text, "DVI %ux%u at %u Hz%s, %u x %u text + status line", hstx_timing.h_active,
		hstx_timing.v_active, hz, HSTX_MODE == 2 ? " (reduced blanking)" : "", screen.cols, TERM_ROWS);
	return hwinfo_record(buf, HWINFO_ST_PRESENT, "Video", text);
}

static size_t serial_record(uint8_t *buf) {
	char text[64];
	unsigned data, stop;
	char parity;
	settings_framing(&settings, &data, &parity, &stop);
	snprintf(text, sizeof text, BOARD_UART_NAME " TX " GP(BOARD_UART_TX_PIN) " / RX " GP(BOARD_UART_RX_PIN)
		", %u %u%c%u (SIO2)", settings_baud_rate(&settings), data, parity, stop);
	return hwinfo_record(buf, HWINFO_ST_PRESENT, "Serial port", text);
}

static size_t flow_record(uint8_t *buf) {
#if !SERIAL_FLOW_CONTROL
	return hwinfo_record(buf, HWINFO_ST_ABSENT, "Flow control", "off (build option SERIAL_FLOW_CONTROL)");
#elif VIDEO_DIAG
	return hwinfo_record(buf, HWINFO_ST_PRESENT, "Flow control", "RTS " GP(BOARD_UART_RTS_PIN) " (CTS: VIDEO_DIAG)");
#else
	return hwinfo_record(buf, HWINFO_ST_PRESENT, "Flow control",
		"RTS " GP(BOARD_UART_RTS_PIN) " / CTS " GP(BOARD_UART_CTS_PIN));
#endif
}

static size_t sd_record(uint8_t *buf) {
	char text[96];
	sdcard_type_t type = sdcard_type();
	if (type == SDCARD_NONE) {
		return hwinfo_record(buf, HWINFO_ST_ABSENT, "SD card", "no card; " BOARD_SD_PINS_TEXT);
	}
	uint32_t mb = sdcard_blocks() / 2048;
	snprintf(text, sizeof text, "%s, %lu.%lu GB; " BOARD_SD_PINS_TEXT, type == SDCARD_SDHC ? "SDHC/SDXC" : "SDSC",
		(unsigned long)(mb / 1024), (unsigned long)(mb % 1024 * 10 / 1024));
	return hwinfo_record(buf, HWINFO_ST_PRESENT, "SD card", text);
}

static size_t usb_record(uint8_t *buf) {
	char text[64];
	unsigned keyboards = usbkbd_count();
	snprintf(text, sizeof text, "native USB port, %u keyboard%s"
#ifdef BOARD_USB_VBUS_EN_PIN
		", VBUS on " GP(BOARD_USB_VBUS_EN_PIN)
#endif
		, keyboards, keyboards == 1 ? "" : "s");
	return hwinfo_record(buf, HWINFO_ST_PRESENT, "USB host", text);
}

static size_t stick_record(uint8_t *buf) {
	usbmsc_status_t u = usbmsc_status();
	if (u.mounted) {
		return hwinfo_record(buf, HWINFO_ST_PRESENT, "USB stick", "mounted (USB: to XLS, XPIP, XERA)");
	}
	return hwinfo_record(buf, HWINFO_ST_ABSENT, "USB stick", u.unusable ? "none usable (FAT only)" : "none");
}

static size_t led_record(uint8_t *buf) {
#ifdef PICO_DEFAULT_LED_PIN
	if (VIDEO_DIAG) {
		return hwinfo_record(buf, HWINFO_ST_PRESENT, "Status LED", GP(PICO_DEFAULT_LED_PIN) ", VIDEO_DIAG's");
	}
	return hwinfo_record(buf, HWINFO_ST_PRESENT, "Status LED", GP(PICO_DEFAULT_LED_PIN) ": disks, serial, heartbeat");
#else
	return hwinfo_record(buf, HWINFO_ST_ABSENT, "Status LED", "none on this board");
#endif
}

static size_t drives_record(uint8_t *buf) {
	char text[DRIVES * (DRIVES_NAME_MAX + 5) + 1];
	size_t n = 0;
	text[0] = 0;
	for (unsigned d = 0; d < DRIVES; ++d) {
		const char *name = drives_name(d);
		if (name) {
			n += (size_t)snprintf(text + n, sizeof text - n, "%s%c: %s", n ? ", " : "", 'A' + d, name);
		}
	}
	return hwinfo_record(buf, n ? HWINFO_ST_PRESENT : HWINFO_ST_ABSENT, "Disk drives", n ? text : "none mounted");
}

size_t cpm_hwinfo(uint8_t item, uint8_t *buf) {
	switch (item) {
	case HWINFO_FIRMWARE:
		return hwinfo_text(buf, "DVI Feather Terminal " TERM_BUILD_ID " (" TERM_BUILD_DATE ")");
	case HWINFO_BOARD:
		return hwinfo_text(buf, PICO_BOARD);
	case HWINFO_CPU:
		return cpu_record(buf);
	case HWINFO_MEMORY:
		return memory_record(buf);
	case HWINFO_PSRAM:
		return psram_record(buf);
	case HWINFO_VIDEO:
		return video_record(buf);
	case HWINFO_SERIAL:
		return serial_record(buf);
	case HWINFO_FLOW_CONTROL:
		return flow_record(buf);
	case HWINFO_SD_CARD:
		return sd_record(buf);
	case HWINFO_USB_HOST:
		return usb_record(buf);
	case HWINFO_USB_STICK:
		return stick_record(buf);
	case HWINFO_LED:
		return led_record(buf);
	case HWINFO_DRIVES:
		return drives_record(buf);
	// Planned (bd 1ls): not driven by this firmware yet.
	case HWINFO_RTC:
		return hwinfo_record(buf, HWINFO_ST_PLANNED, "Real-time clock",
			"not supported yet: the clock starts at a fixed date at power-on");
	case HWINFO_BELL:
		return hwinfo_record(buf, HWINFO_ST_PLANNED, "Bell", "not supported yet: BEL is silent");
	case HWINFO_NEOPIXEL:
		return hwinfo_record(buf, HWINFO_ST_PLANNED, "RGB status LED", "not supported yet");
	case HWINFO_SD_DETECT:
		return hwinfo_record(buf, HWINFO_ST_PLANNED, "SD card detect", "not supported yet");
	case HWINFO_BUTTON:
		return hwinfo_record(buf, HWINFO_ST_PLANNED, "User button", "not supported yet");
	case HWINFO_BATTERY:
		return hwinfo_record(buf, HWINFO_ST_PLANNED, "Battery", "not supported yet");
	default:
		return 0;
	}
}
