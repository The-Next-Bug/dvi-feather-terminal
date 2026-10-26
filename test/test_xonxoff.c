#include "test.h"
#include "xonxoff.h"

static void test_host_stops_and_starts_us(void) {
	xonxoff_t x;
	xonxoff_init(&x, 2048);
	CHECK(!x.stopped);
	CHECK(!xonxoff_received(&x, 'a'));
	CHECK(xonxoff_received(&x, XONXOFF_XOFF));
	CHECK(x.stopped);
	CHECK(xonxoff_received(&x, XONXOFF_XOFF)); // again: still stopped
	CHECK(x.stopped);
	CHECK(!xonxoff_received(&x, 0x93)); // XOFF with bit 7: data
	CHECK(xonxoff_received(&x, XONXOFF_XON));
	CHECK(!x.stopped);
	CHECK(xonxoff_received(&x, XONXOFF_XON)); // XON while going: dropped too
	CHECK(!x.stopped);
}

static void test_xoff_at_half_xon_when_drained(void) {
	xonxoff_t x;
	xonxoff_init(&x, 2048);
	CHECK_EQ(xonxoff_level(&x, 0), 0);
	CHECK_EQ(xonxoff_level(&x, 1023), 0);
	CHECK_EQ(xonxoff_level(&x, 1024), XONXOFF_XOFF);
	CHECK(x.held);
	CHECK_EQ(xonxoff_level(&x, 1100), 0); // once
	CHECK_EQ(xonxoff_level(&x, 600), 0);  // draining, not yet low
	CHECK_EQ(xonxoff_level(&x, 257), 0);
	CHECK_EQ(xonxoff_level(&x, 256), XONXOFF_XON);
	CHECK(!x.held);
	CHECK_EQ(xonxoff_level(&x, 0), 0); // once
	CHECK_EQ(xonxoff_level(&x, 1024), XONXOFF_XOFF); // and again
}

static void test_xoff_again_while_bytes_keep_coming(void) {
	xonxoff_t x;
	xonxoff_init(&x, 2048);
	CHECK_EQ(xonxoff_level(&x, 1030), XONXOFF_XOFF);
	CHECK_EQ(xonxoff_level(&x, 1285), 0);
	CHECK_EQ(xonxoff_level(&x, 1286), XONXOFF_XOFF); // 256 more: the host did not stop
	CHECK_EQ(xonxoff_level(&x, 1500), 0);
	CHECK_EQ(xonxoff_level(&x, 1542), XONXOFF_XOFF);
	CHECK_EQ(xonxoff_level(&x, 200), XONXOFF_XON);
}

static void test_receiving_does_not_touch_our_side(void) {
	xonxoff_t x;
	xonxoff_init(&x, 2048);
	CHECK_EQ(xonxoff_level(&x, 1024), XONXOFF_XOFF);
	CHECK(xonxoff_received(&x, XONXOFF_XON)); // the host's XON is for its own side
	CHECK(x.held);
	CHECK_EQ(xonxoff_level(&x, 100), XONXOFF_XON);
}

int main(void) {
	RUN(test_host_stops_and_starts_us);
	RUN(test_xoff_at_half_xon_when_drained);
	RUN(test_xoff_again_while_bytes_keep_coming);
	RUN(test_receiving_does_not_touch_our_side);
	return tests_done();
}
