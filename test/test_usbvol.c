// Which USB stick is volume 1 (usbvol.c).

#include "test.h"
#include "usbvol.h"

static usbvol_t v;

// Run the main loop's side: the next action, done as asked (mount_ok:
// whether a mount works). Returns the action; *dev the stick mounted.
static usbvol_action_t step(bool mount_ok, uint8_t *dev) {
	uint8_t d = 0;
	usbvol_action_t a = usbvol_next(&v, &d);
	if (a == USBVOL_MOUNT) {
		usbvol_mounted(&v, mount_ok);
	} else if (a == USBVOL_UNMOUNT) {
		usbvol_unmounted(&v);
	}
	if (dev) {
		*dev = d;
	}
	return a;
}

static void test_one_stick(void) {
	usbvol_init(&v);
	CHECK_EQ(step(true, NULL), USBVOL_NONE);
	CHECK_EQ(usbvol_io_dev(&v), 0);
	usbvol_attach(&v, 3);
	uint8_t dev;
	CHECK_EQ(usbvol_next(&v, &dev), USBVOL_MOUNT);
	CHECK_EQ(dev, 3);
	CHECK_EQ(usbvol_io_dev(&v), 3); // the mount reads it
	usbvol_mounted(&v, true);
	CHECK_EQ(step(true, NULL), USBVOL_NONE);
	CHECK_EQ(usbvol_io_dev(&v), 3);
	CHECK_EQ(usbvol_waiting(&v), 0);
	// Pulled: no more I/O to it at once; the volume is unmounted next.
	usbvol_detach(&v, 3);
	CHECK_EQ(usbvol_io_dev(&v), 0);
	CHECK_EQ(step(true, NULL), USBVOL_UNMOUNT);
	CHECK_EQ(step(true, NULL), USBVOL_NONE);
	CHECK_EQ(v.n, 0);
}

// A second stick waits; it takes over only after the first's volume is
// unmounted, never while files may still be open on it.
static void test_second_stick_waits(void) {
	usbvol_init(&v);
	usbvol_attach(&v, 2);
	uint8_t dev;
	CHECK_EQ(step(true, &dev), USBVOL_MOUNT);
	CHECK_EQ(dev, 2);
	usbvol_attach(&v, 5);
	CHECK_EQ(step(true, NULL), USBVOL_NONE);
	CHECK_EQ(usbvol_io_dev(&v), 2);
	CHECK_EQ(usbvol_waiting(&v), 1);
	// The second goes and comes back: still waiting, still not the volume.
	usbvol_detach(&v, 5);
	CHECK_EQ(usbvol_waiting(&v), 0);
	usbvol_attach(&v, 5);
	CHECK_EQ(step(true, NULL), USBVOL_NONE);
	CHECK_EQ(usbvol_io_dev(&v), 2);
	// The first goes: unmount, then the second.
	usbvol_detach(&v, 2);
	CHECK_EQ(usbvol_io_dev(&v), 0);
	CHECK_EQ(step(true, NULL), USBVOL_UNMOUNT);
	CHECK_EQ(usbvol_io_dev(&v), 0);
	CHECK_EQ(step(true, &dev), USBVOL_MOUNT);
	CHECK_EQ(dev, 5);
	CHECK_EQ(usbvol_io_dev(&v), 5);
	CHECK_EQ(usbvol_waiting(&v), 0);
}

// In arrival order: of three, the oldest waiting takes over.
static void test_order(void) {
	usbvol_init(&v);
	usbvol_attach(&v, 4);
	usbvol_attach(&v, 2);
	usbvol_attach(&v, 7);
	uint8_t dev;
	CHECK_EQ(step(true, &dev), USBVOL_MOUNT);
	CHECK_EQ(dev, 4);
	CHECK_EQ(usbvol_waiting(&v), 2);
	usbvol_detach(&v, 4);
	CHECK_EQ(step(true, NULL), USBVOL_UNMOUNT);
	CHECK_EQ(step(true, &dev), USBVOL_MOUNT);
	CHECK_EQ(dev, 2);
	// Attached twice (a repeated callback): once.
	usbvol_attach(&v, 7);
	CHECK_EQ(usbvol_waiting(&v), 1);
}

// A stick that will not mount is passed over (and counted), the next one
// tried; plugged in again, it is tried again.
static void test_unusable(void) {
	usbvol_init(&v);
	usbvol_attach(&v, 2);
	usbvol_attach(&v, 3);
	uint8_t dev;
	CHECK_EQ(step(false, &dev), USBVOL_MOUNT);
	CHECK_EQ(dev, 2);
	CHECK_EQ(usbvol_io_dev(&v), 0);
	CHECK_EQ(usbvol_unusable(&v), 1);
	CHECK_EQ(step(true, &dev), USBVOL_MOUNT);
	CHECK_EQ(dev, 3);
	CHECK_EQ(step(true, NULL), USBVOL_NONE);
	CHECK_EQ(usbvol_waiting(&v), 0); // the bad one does not wait
	// Both go; the bad one comes back: tried again.
	usbvol_detach(&v, 2);
	CHECK_EQ(usbvol_unusable(&v), 0);
	CHECK_EQ(step(true, NULL), USBVOL_NONE); // the volume's stick is still in
	usbvol_detach(&v, 3);
	CHECK_EQ(step(true, NULL), USBVOL_UNMOUNT);
	usbvol_attach(&v, 2);
	CHECK_EQ(step(true, &dev), USBVOL_MOUNT);
	CHECK_EQ(dev, 2);
}

// Pulled while it was being mounted (the mount fails or, if it got far
// enough, succeeds): either way it ends unmounted, not marked bad.
static void test_pulled_while_mounting(void) {
	usbvol_init(&v);
	usbvol_attach(&v, 2);
	uint8_t dev;
	CHECK_EQ(usbvol_next(&v, &dev), USBVOL_MOUNT);
	usbvol_detach(&v, 2);
	usbvol_mounted(&v, false);
	CHECK_EQ(step(true, NULL), USBVOL_NONE);
	CHECK_EQ(v.active, 0);
	usbvol_attach(&v, 2);
	CHECK_EQ(usbvol_next(&v, &dev), USBVOL_MOUNT);
	usbvol_detach(&v, 2);
	usbvol_mounted(&v, true);
	CHECK_EQ(usbvol_io_dev(&v), 0);
	CHECK_EQ(step(true, NULL), USBVOL_UNMOUNT);
	CHECK_EQ(step(true, NULL), USBVOL_NONE);
	CHECK_EQ(v.mounted, false);
}

// No room for more than USBVOL_MAX: the extra is ignored, not a crash.
static void test_full(void) {
	usbvol_init(&v);
	for (uint8_t d = 1; d <= USBVOL_MAX + 2; ++d) {
		usbvol_attach(&v, d);
	}
	CHECK_EQ(v.n, USBVOL_MAX);
	usbvol_detach(&v, USBVOL_MAX + 2); // never attached: nothing
	CHECK_EQ(v.n, USBVOL_MAX);
	usbvol_attach(&v, 0); // not a device address
	CHECK_EQ(v.n, USBVOL_MAX);
}

int main(void) {
	RUN(test_one_stick);
	RUN(test_second_stick_waits);
	RUN(test_order);
	RUN(test_unusable);
	RUN(test_pulled_while_mounting);
	RUN(test_full);
	return tests_done();
}
