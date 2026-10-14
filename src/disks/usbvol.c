#include "usbvol.h"

#include <string.h>

void usbvol_init(usbvol_t *v) {
	memset(v, 0, sizeof *v);
}

static int index_of(const usbvol_t *v, uint8_t dev) {
	for (unsigned i = 0; i < v->n; ++i)
		if (v->dev[i] == dev)
			return (int)i;
	return -1;
}

void usbvol_attach(usbvol_t *v, uint8_t dev) {
	if (!dev || index_of(v, dev) >= 0 || v->n == USBVOL_MAX)
		return;
	v->dev[v->n] = dev;
	v->bad[v->n] = false;
	++v->n;
}

void usbvol_detach(usbvol_t *v, uint8_t dev) {
	int i = index_of(v, dev);
	if (i < 0)
		return;
	for (unsigned j = (unsigned)i; j + 1 < v->n; ++j) {
		v->dev[j] = v->dev[j + 1];
		v->bad[j] = v->bad[j + 1];
	}
	--v->n;
	if (dev == v->active)
		v->gone = true;
}

usbvol_action_t usbvol_next(usbvol_t *v, uint8_t *dev) {
	if (v->gone) {
		if (v->mounted)
			return USBVOL_UNMOUNT;
		v->active = 0;
		v->gone = false;
	}
	if (v->active)
		return USBVOL_NONE;
	for (unsigned i = 0; i < v->n; ++i)
		if (!v->bad[i]) {
			v->active = *dev = v->dev[i];
			return USBVOL_MOUNT;
		}
	return USBVOL_NONE;
}

void usbvol_mounted(usbvol_t *v, bool ok) {
	if (ok) {
		v->mounted = true; // if it went meanwhile, usbvol_next unmounts it
		return;
	}
	int i = index_of(v, v->active);
	if (i >= 0)
		v->bad[i] = true;
	v->active = 0;
	v->gone = false;
}

void usbvol_unmounted(usbvol_t *v) {
	v->active = 0;
	v->mounted = false;
	v->gone = false;
}

uint8_t usbvol_io_dev(const usbvol_t *v) {
	return v->gone ? 0 : v->active;
}

unsigned usbvol_waiting(const usbvol_t *v) {
	unsigned n = 0;
	for (unsigned i = 0; i < v->n; ++i)
		n += !v->bad[i] && v->dev[i] != v->active;
	return n;
}

unsigned usbvol_unusable(const usbvol_t *v) {
	unsigned n = 0;
	for (unsigned i = 0; i < v->n; ++i)
		n += v->bad[i];
	return n;
}
