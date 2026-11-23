// The WiFi modem's start-up on the board (bd xir.17.2): src/modem/modem.c
// on the terminal UART and the modem's RST line, from power-on in WiFi
// modem mode (the Port setting, WIFI_MODEM builds). While it runs the line
// is its own: the terminal, CP/M's SIO2, the printer and keys leave it
// alone (modem_glue_busy). Cold: in flash (ld/).

#include "terminal.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "modem_lines.h"
#include "palette.h"
#include "serial.h"
#include "video.h"

#if WIFI_MODEM

#include "espflash.h"
#include "ff.h"
#include "modem.h"
#include "zmi.h"

static modem_t modem;
static bool started;

// Flashing the module (bd xir.17.10): the flasher has the line meanwhile.
static espflash_t flash;
static bool flashing;
static void flash_poll(uint32_t now);

static void io_reset(void *ctx, bool hold) {
	(void)ctx;
	modem_hold_reset(hold);
}

static void io_baud(void *ctx, unsigned baud) {
	(void)ctx;
	serial_configure(baud, 8, 'N', 1); // RTS/CTS stays as apply_line set it
}

static void io_send(void *ctx, const char *text) {
	(void)ctx;
	size_t n = strlen(text);
	while (n) { // short commands: the queue takes them, waiting only if full
		size_t sent = serial_write((const uint8_t *)text, n);
		text += sent;
		n -= sent;
		if (n) {
			serial_poll();
		}
	}
}

static bool io_drained(void *ctx) {
	(void)ctx;
	return serial_tx_drained();
}

static const modem_io_t io = {NULL, io_reset, io_baud, io_send, io_drained};

void modem_glue_start(void) {
	if (!settings_wifi(&settings)) {
		return;
	}
	modem_config_t cfg = {
		.baud = SETTINGS_MODEM_BAUD,
		.echo = settings.mode == SETTINGS_MODE_CPM, // Hayes for CP/M programs; quiet for the terminal
		.ssid = settings.wifi_ssid,
		.password = settings.wifi_password,
	};
	modem_init(&modem, &io, &cfg);
	modem_start(&modem, to_ms_since_boot(get_absolute_time()));
	started = true;
	modem_owns_line = true;
}

void modem_glue_poll(void) {
	if (!modem_glue_busy()) {
		return;
	}
	uint32_t now = to_ms_since_boot(get_absolute_time());
	if (flashing) {
		flash_poll(now);
		return;
	}
	uint8_t buf[64];
	size_t n;
	while ((n = serial_read(buf, sizeof buf)) > 0) {
		for (size_t i = 0; i < n; ++i) {
			modem_feed(&modem, buf[i], now);
		}
	}
	modem_poll(&modem, now);
	serial_poll();
	modem_owns_line = modem_busy(&modem);
}

const char *modem_glue_notice(void) {
	static char text[40];
	if (!modem_glue_busy()) {
		return NULL;
	}
	if (flashing) {
		snprintf(text, sizeof text, "WiFi modem: %s %u%%", espflash_step_name(&flash),
			flash.total ? (unsigned)((uint64_t)flash.written * 100 / flash.total) : 0u);
	} else {
		snprintf(text, sizeof text, "WiFi modem: %s", modem_step_name(&modem));
	}
	return text;
}

modem_status_t modem_glue_status(void) {
	if (!started) {
		return MODEM_STATUS_NONE;
	}
	if (flashing) {
		return MODEM_STATUS_STARTING;
	}
	switch (modem.state) {
	case MODEM_STARTING:
		return MODEM_STATUS_STARTING;
	case MODEM_READY:
		return modem_joined(&modem) ? MODEM_STATUS_WIFI : MODEM_STATUS_NO_WIFI;
	case MODEM_FAILED:
		return MODEM_STATUS_FAILED;
	default:
		return MODEM_STATUS_NONE;
	}
}

bool modem_glue_carrier(void) {
	return started && modem_dcd();
}

// HWINFO's record (port 12): the module, what the start-up found, its
// lines. HWINFO wraps the detail; a long network name cuts the lines off.
#define PIN(n) "GP" BOARD_STR(n)
#define MODEM_LINES BOARD_UART_NAME " " BOARD_STR(BOARD_MODEM_BAUD) " RTS/CTS, DCD/DTR/RST/BOOT " \
	PIN(BOARD_MODEM_DCD_PIN) "/" BOARD_STR(BOARD_MODEM_DTR_PIN) "/" BOARD_STR(BOARD_MODEM_RST_PIN) "/" \
	BOARD_STR(BOARD_MODEM_BOOT_PIN)
#define MODEM_CHIP BOARD_MODEM_CHIP_NAME " (" BOARD_MODEM_NAME ")"

size_t modem_record(uint8_t *buf) {
	char text[192]; // the longest names fit; hwinfo_record cuts to HWINFO_MAX
	uint8_t st = HWINFO_ST_PRESENT;
	const char *version = modem.version[0] ? modem.version : "?";
	switch (modem_glue_status()) {
	case MODEM_STATUS_NONE:
		return hwinfo_record(buf, HWINFO_ST_ABSENT, "WiFi modem",
			MODEM_CHIP ": off, Port is the serial line (SET-UP); " MODEM_LINES);
	case MODEM_STATUS_STARTING:
		snprintf(text, sizeof text, MODEM_CHIP ": %s; " MODEM_LINES, modem_step_name(&modem));
		break;
	case MODEM_STATUS_WIFI:
		snprintf(text, sizeof text, MODEM_CHIP ", Zimodem %s, on %s as %s%s; " MODEM_LINES, version, modem.ssid,
			modem.ip, modem_glue_carrier() ? ", connected" : "");
		break;
	case MODEM_STATUS_NO_WIFI:
		snprintf(text, sizeof text, MODEM_CHIP ", Zimodem %s, no network (SET-UP's Modem panel); " MODEM_LINES,
			version);
		break;
	default:
		st = HWINFO_ST_NOT_FOUND;
		snprintf(text, sizeof text, MODEM_CHIP " did not answer (SET-UP's Modem panel: Reset modem); " MODEM_LINES);
		break;
	}
	return hwinfo_record(buf, st, "WiFi modem", text);
}

// ---- the module's firmware from ZIMODEM.ZMI (bd xir.17.10) ----
//
// Update firmware on the Modem panel: the file is looked for on the card,
// then the stick, read whole and checked (zmi.h: the container, the ESP
// image, the chip, the module, its flash) before anything is erased; a
// second Return writes it through the ROM loader (espflash.h), a piece of
// the file at a time from modem_glue_poll, then the start-up runs and the
// version the module reports is compared with the file's.

#define ZMI_NAME "ZIMODEM.ZMI"

static FIL zmi_file;
static bool zmi_open;
static zmi_reader_t zmi;
static uint8_t zmi_buf[512];
static UINT zmi_len, zmi_pos;
static bool zmi_eof;
static char zmi_path[16];
static char zmi_version[ZMI_VERSION_MAX + 1];
static uint32_t zmi_image_bytes;
static char update_message[128]; // shown in the panel's 76 columns
static bool check_version; // the start-up after a flash: compare its ATI4

static void zmi_close(void) {
	if (zmi_open) {
		f_close(&zmi_file);
		zmi_open = false;
	}
	fw_unmount(false);
}

// The file on the card ("0:"), else the stick ("1:"): open, true.
static bool zmi_find(const char **where) {
	char info[64];
	bool card = fw_mount(info, sizeof info);
	static const char *const paths[] = {"0:/" ZMI_NAME, "1:/" ZMI_NAME};
	static const char *const names[] = {"SD card", "USB stick"};
	for (unsigned v = card ? 0 : 1; v < 2; ++v) {
		if (v == 1 && !fw_stick()) {
			break;
		}
		if (f_open(&zmi_file, paths[v], FA_READ) == FR_OK) {
			zmi_open = true;
			snprintf(zmi_path, sizeof zmi_path, "%s", paths[v]);
			*where = names[v];
			return true;
		}
	}
	return false;
}

static void on_part(void *ctx, uint32_t address, uint32_t length) {
	(void)ctx;
	espflash_part(&flash, address, length, to_ms_since_boot(get_absolute_time()));
}

static void on_data(void *ctx, uint32_t address, const uint8_t *data, size_t len) {
	(void)ctx;
	(void)address;
	espflash_data(&flash, data, len, to_ms_since_boot(get_absolute_time()));
}

// Return on Update firmware: find the file and check all of it.
static void update_check(void) {
	const char *where;
	setup.modem.update_ready = false;
	if (!zmi_find(&where)) {
		zmi_close();
		setup.modem.message = "No " ZMI_NAME " on the SD card or a USB stick";
		return;
	}
	zmi_reader_t r;
	zmi_init(&r, NULL, NULL);
	UINT n;
	while (f_read(&zmi_file, zmi_buf, sizeof zmi_buf, &n) == FR_OK && n > 0 && zmi_feed(&r, zmi_buf, n) == ZMI_OK) {
	}
	zmi_result_t res = zmi_finish(&r);
	if (res == ZMI_OK) {
		res = zmi_check_target(&r.info, ZMI_CHIP_ESP8266, BOARD_MODEM_NAME, BOARD_MODEM_FLASH_KB * 1024u);
	}
	zmi_close();
	if (res != ZMI_OK) {
		snprintf(update_message, sizeof update_message, ZMI_NAME " on the %s: %s; not used", where,
			zmi_result_text(res));
		setup.modem.message = update_message;
		return;
	}
	snprintf(zmi_version, sizeof zmi_version, "%s", r.info.version);
	zmi_image_bytes = r.info.image_bytes;
	snprintf(update_message, sizeof update_message, "%s: Zimodem %s. Return again: write it", where,
		r.info.version); // the module's own is on the Firmware row
	setup.modem.message = update_message;
	setup.modem.update_ready = true;
}

static void flash_reset(void *ctx, bool hold) {
	(void)ctx;
	modem_hold_reset(hold);
}

static void flash_boot(void *ctx, bool hold) {
	(void)ctx;
	modem_hold_boot(hold);
}

static void flash_send(void *ctx, const uint8_t *data, size_t len) {
	(void)ctx;
	while (len) {
		size_t sent = serial_write(data, len);
		data += sent;
		len -= sent;
		if (len) {
			serial_poll();
		}
	}
}

static const espflash_io_t flash_io = {NULL, flash_reset, flash_boot, io_baud, flash_send};

// The second Return: open the file again and start writing.
static void update_start(uint32_t now) {
	const char *where;
	if (!zmi_find(&where)) {
		zmi_close();
		setup.modem.message = ZMI_NAME " is gone";
		return;
	}
	zmi_init(&zmi, on_data, NULL);
	zmi_set_part_callback(&zmi, on_part);
	zmi_len = zmi_pos = 0;
	zmi_eof = false;
	serial_set_flow(SERIAL_FLOW_NONE); // the ROM loader has none
	espflash_init(&flash, &flash_io);
	espflash_start(&flash, zmi_image_bytes, now);
	flashing = true;
	modem_owns_line = true;
	setup.modem.flashing = true;
	setup.modem.message = update_message;
	update_message[0] = '\0'; // the progress, from modem_panel_service
}

// Done (or failed): the line back to the modem, which starts again.
static void update_end(uint32_t now) {
	zmi_close();
	flashing = false;
	setup.modem.flashing = false;
	serial_set_flow(SERIAL_FLOW_RTSCTS);
	if (flash.state == ESPFLASH_DONE) {
		check_version = true;
		snprintf(update_message, sizeof update_message, "Written; the modem starts (it checks the version)");
	} else {
		snprintf(update_message, sizeof update_message, "Not written: %.60s", flash.problem ? flash.problem : "?");
	}
	setup.modem.message = update_message;
	modem_start(&modem, now); // a reset with BOOT high: whatever the flash holds runs
	modem_owns_line = true;
}

static void flash_poll(uint32_t now) {
	uint8_t buf[64];
	size_t n;
	while ((n = serial_read(buf, sizeof buf)) > 0) {
		for (size_t i = 0; i < n; ++i) {
			espflash_feed(&flash, buf[i], now);
		}
	}
	espflash_poll(&flash, now);
	// The file, a piece at a time: never more than the flasher has room for.
	size_t room;
	while (!zmi_eof && (room = espflash_room(&flash)) > 0) {
		if (zmi_pos == zmi_len) {
			if (f_read(&zmi_file, zmi_buf, sizeof zmi_buf, &zmi_len) != FR_OK || zmi_len == 0) {
				zmi_eof = true;
				break;
			}
			zmi_pos = 0;
		}
		UINT piece = zmi_len - zmi_pos < room ? zmi_len - zmi_pos : (UINT)room;
		if (zmi_feed(&zmi, zmi_buf + zmi_pos, piece) != ZMI_OK) {
			espflash_abort(&flash, "the file changed since its check");
			break;
		}
		zmi_pos += piece;
	}
	if (zmi_eof && flash.state == ESPFLASH_READY) {
		if (zmi_finish(&zmi) == ZMI_OK) {
			espflash_finish(&flash, now);
		} else {
			espflash_abort(&flash, "the file changed since its check");
		}
	}
	serial_poll();
	if (flash.state == ESPFLASH_DONE || flash.state == ESPFLASH_FAILED) {
		update_end(now);
	}
}

// After a flash, the start-up's ATI4 against the file's version.
static void version_service(void) {
	if (!check_version || modem_busy(&modem)) {
		return;
	}
	check_version = false;
	if (modem.state == MODEM_READY && strcmp(modem.version, zmi_version) == 0) {
		snprintf(update_message, sizeof update_message, "Updated: Zimodem %s", zmi_version);
	} else {
		snprintf(update_message, sizeof update_message, "Written; the module reports %.23s, not %.23s",
			modem.version[0] ? modem.version : "nothing", zmi_version);
	}
	setup.modem.message = update_message;
}

// ---- the Modem panel in SET-UP (Return on the Port row, bd xir.17.4) ----

static_assert(MODEMPANEL_ROWS <= SETUP_ROWS, "the Modem panel fits SET-UP's rows");

static void panel_status(modempanel_status_t *st) {
	*st = (modempanel_status_t){
		.state = started ? modem_step_name(&modem) : "not started",
		.busy = modem_glue_busy(),
		.joined = started && modem_joined(&modem),
		.ssid = modem.ssid,
		.ip = modem.ip,
		.version = modem.version,
		.rom = modem.rom,
		.problem = modem.problem,
	};
}

// What the panel shows of the modem, to redraw it when that changes.
static char panel_shown[256];

static void panel_fingerprint(char *out, size_t size) {
	snprintf(out, size, "%s|%d|%s|%s|%s|%s|%d|%s", modem_step_name(&modem), modem_glue_busy(), modem.ip, modem.ssid,
		modem.version, modem.problem ? modem.problem : "", flashing, setup.modem.message ? setup.modem.message : "");
}

static void modem_draw_panel(void) {
	settings_colors_t c = settings_colors(&setup.ui.values);
	for (unsigned r = 0; r < SETUP_ROWS - MODEMPANEL_ROWS; ++r) {
		screen_clear_line(&screen, SETUP_TOP + r, PALETTE_TEXT, PALETTE_BACKGROUND);
	}
	panel_status(&setup.modem.status);
	panel_fingerprint(panel_shown, sizeof panel_shown);
	modempanel_draw(&setup.modem, &screen, SETUP_TOP + SETUP_ROWS - MODEMPANEL_ROWS, PALETTE_TEXT,
		PALETTE_BACKGROUND, c.status_bg);
}

void modem_panel_open(void) {
	modempanel_open(&setup.modem, &setup.ui.values);
	setup.modem_open = true;
	setup.modem_work = MODEMPANEL_NONE;
	modem_draw_panel();
}

void modem_panel_feed(uint8_t c, uint32_t now) {
	modempanel_action_t a = modempanel_input(&setup.modem, c, now);
	if (a == MODEMPANEL_REDRAW) {
		modem_draw_panel();
	} else if (a != MODEMPANEL_NONE) {
		setup.modem_work = a;
	}
}

// Return on Network or Password: the network saved (into SET-UP's edited
// settings too, so its own Return keeps it), then pushed to the module.
static void panel_join(uint32_t now) {
	const settings_t *v = &setup.modem.values;
	if (v->wifi_ssid[0] && !modem_network_sendable(v->wifi_ssid, v->wifi_password)) {
		setup.modem.message = "Zimodem cannot take a comma or quote in the name, a quote in the password";
		return;
	}
	settings_t s = settings;
	settings_set_wifi_ssid(&s, v->wifi_ssid);
	settings_set_wifi_password(&s, v->wifi_password);
	setup.modem.message = "Saving...";
	modem_draw_panel();
	video_update(&screen);
	if (!settings_store(&s)) {
		setup.modem.message = "Saving to flash failed";
		return;
	}
	settings_set_wifi_ssid(&setup.ui.values, v->wifi_ssid);
	settings_set_wifi_password(&setup.ui.values, v->wifi_password);
	modem_join(&modem, now); // cfg points at settings' texts, saved just now
	modem_owns_line = modem_busy(&modem);
	setup.modem.message = v->wifi_ssid[0] ? "Saved; joining the network" : "Saved: the module keeps its own network";
}

void modem_panel_service(void) {
	uint32_t now = to_ms_since_boot(get_absolute_time());
	modempanel_action_t work = setup.modem_work;
	setup.modem_work = MODEMPANEL_NONE;
	version_service();
	if (flashing) {
		snprintf(update_message, sizeof update_message, "%s: %u of %u KB; do not switch off",
			espflash_step_name(&flash), (unsigned)(flash.written / 1024), (unsigned)(flash.total / 1024));
	}
	if (work == MODEMPANEL_NONE) {
		work = modempanel_tick(&setup.modem, now);
	}
	switch (work) {
	case MODEMPANEL_CANCEL:
		setup.modem_open = false;
		setup_draw_panel();
		return;
	case MODEMPANEL_JOIN:
		panel_join(now);
		break;
	case MODEMPANEL_RESET:
		modem_start(&modem, now);
		modem_owns_line = true;
		setup.modem.message = "Resetting it; the line is the modem's until it is set up";
		break;
	case MODEMPANEL_DEFAULTS:
		modem_factory(&modem, now);
		modem_owns_line = modem_busy(&modem);
		setup.modem.message = "Its factory settings, then ours: this takes a while";
		break;
	case MODEMPANEL_UPDATE:
		if (modem_glue_busy()) {
			setup.modem.message = "Wait for the modem's start-up first";
		} else {
			update_check();
		}
		break;
	case MODEMPANEL_FLASH:
		update_start(now);
		break;
	default:
		break;
	}
	char now_shown[sizeof panel_shown];
	panel_fingerprint(now_shown, sizeof now_shown);
	if (work != MODEMPANEL_NONE || strcmp(now_shown, panel_shown) != 0) {
		modem_draw_panel();
	}
}

#else

void modem_glue_start(void) {
}

const char *modem_glue_notice(void) {
	return NULL;
}

void modem_glue_poll(void) {
}

modem_status_t modem_glue_status(void) {
	return MODEM_STATUS_NONE;
}

bool modem_glue_carrier(void) {
	return false;
}

size_t modem_record(uint8_t *buf) {
	(void)buf;
	return 0;
}

#endif
