#ifndef MODEM_LINES_H
#define MODEM_LINES_H

// The WiFi modem's control lines (BOARD_MODEM_*_PIN, bd xir.15): DCD in,
// DTR out, and the module's RST and BOOT (open drain: driven low or
// released to the module's pull-ups). Core 0 only. Without the WiFi modem
// mode (WIFI_MODEM: a board with a modem and PSRAM) every call is a no-op,
// the pins stay as at reset, and modem_dcd() reads false.

#include <stdbool.h>

// First thing in main(): DTR active (low), RST and BOOT released, DCD an
// input; pulls off on all four (the module has its own, and the pads'
// reset pull-downs fight them: RP2350-E9).
void modem_lines_init(void);

// DTR: true = active (driven low).
void modem_set_dtr(bool active);

// The module's carrier: true while it drives DCD low (a call is open).
bool modem_dcd(void);

// RST and BOOT: true = held low, false = released.
void modem_hold_reset(bool hold);
void modem_hold_boot(bool hold);

#endif
