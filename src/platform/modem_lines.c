#include "modem_lines.h"

#include "board.h"
#include "hardware/gpio.h"

#if BOARD_HAS_MODEM

// An open-drain line: the output latch stays 0, the direction switches
// between driving it low and leaving it to the module's pull-up.
static void open_drain_init(unsigned pin) {
	gpio_init(pin);
	gpio_disable_pulls(pin);
	gpio_put(pin, 0);
	gpio_set_dir(pin, GPIO_IN);
}

void modem_lines_init(void) {
	gpio_init(BOARD_MODEM_DTR_PIN);
	gpio_disable_pulls(BOARD_MODEM_DTR_PIN);
	gpio_put(BOARD_MODEM_DTR_PIN, 0); // active
	gpio_set_dir(BOARD_MODEM_DTR_PIN, GPIO_OUT);
	gpio_init(BOARD_MODEM_DCD_PIN);
	gpio_disable_pulls(BOARD_MODEM_DCD_PIN);
	gpio_set_dir(BOARD_MODEM_DCD_PIN, GPIO_IN);
	open_drain_init(BOARD_MODEM_RST_PIN);
	open_drain_init(BOARD_MODEM_BOOT_PIN);
}

void modem_set_dtr(bool active) {
	gpio_put(BOARD_MODEM_DTR_PIN, !active);
}

bool modem_dcd(void) {
	return !gpio_get(BOARD_MODEM_DCD_PIN);
}

void modem_hold_reset(bool hold) {
	gpio_set_dir(BOARD_MODEM_RST_PIN, hold ? GPIO_OUT : GPIO_IN);
}

void modem_hold_boot(bool hold) {
	gpio_set_dir(BOARD_MODEM_BOOT_PIN, hold ? GPIO_OUT : GPIO_IN);
}

#else

void modem_lines_init(void) {
}

void modem_set_dtr(bool active) {
	(void)active;
}

bool modem_dcd(void) {
	return false;
}

void modem_hold_reset(bool hold) {
	(void)hold;
}

void modem_hold_boot(bool hold) {
	(void)hold;
}

#endif
