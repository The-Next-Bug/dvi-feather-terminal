#include "blank.h"
#include "test.h"

static void test_blanks_after_delay(void) {
	blank_t b;
	blank_init(&b, 1000, 50);
	CHECK(!blank_tick(&b, 50));
	CHECK(!blank_tick(&b, 1049));
	CHECK(blank_tick(&b, 1050));
	CHECK(blank_tick(&b, 5000)); // stays blanked
}

static void test_key_restarts_the_delay(void) {
	blank_t b;
	blank_init(&b, 1000, 0);
	CHECK(!blank_activity(&b, 900)); // not blanked: the key is used
	CHECK(!blank_tick(&b, 1500));
	CHECK(blank_tick(&b, 1900));
}

static void test_waking_key_is_dropped(void) {
	blank_t b;
	blank_init(&b, 1000, 0);
	CHECK(blank_tick(&b, 1000));
	CHECK(blank_activity(&b, 1200)); // woke the screen: dropped
	CHECK(!blank_tick(&b, 1200));
	CHECK(!blank_activity(&b, 1300)); // the next one is typed
	CHECK(!blank_tick(&b, 2299));
	CHECK(blank_tick(&b, 2300));
}

static void test_never(void) {
	blank_t b;
	blank_init(&b, 0, 0);
	CHECK(!blank_tick(&b, 0));
	CHECK(!blank_tick(&b, UINT32_MAX));
	CHECK(!blank_activity(&b, 10));
}

static void test_clock_wraps(void) {
	blank_t b;
	blank_init(&b, 1000, UINT32_MAX - 100);
	CHECK(!blank_tick(&b, 800)); // 901 ms later
	CHECK(blank_tick(&b, 899));  // 1000 ms later
}

static void test_init_shows_the_screen(void) {
	blank_t b;
	blank_init(&b, 1000, 0);
	CHECK(blank_tick(&b, 2000));
	blank_init(&b, 60000, 2000); // the setting changed
	CHECK(!blank_tick(&b, 2000));
}

int main(void) {
	RUN(test_blanks_after_delay);
	RUN(test_key_restarts_the_delay);
	RUN(test_waking_key_is_dropped);
	RUN(test_never);
	RUN(test_clock_wraps);
	RUN(test_init_shows_the_screen);
	return tests_done();
}
