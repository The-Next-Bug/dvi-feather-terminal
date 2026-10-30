#ifndef TEST_H
#define TEST_H

// Minimal test harness: CHECK records a failure and carries on; tests_done()
// reports and returns the process exit code. TEST_ONLY (environment), a
// comma-separated list, runs only the tests whose names contain one of its
// words (TEST_ONLY=fat32,install,mpm ./test_machine: the ones the MP/M tests
// need first, then those); later tests may depend on earlier ones.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
		++test_failures; \
	} \
} while (0)

#define CHECK_EQ(a, b) do { \
	long long a_ = (long long)(a), b_ = (long long)(b); \
	if (a_ != b_) { \
		fprintf(stderr, "%s:%d: CHECK_EQ failed: %s == %s (%lld != %lld)\n", \
			__FILE__, __LINE__, #a, #b, a_, b_); \
		++test_failures; \
	} \
} while (0)

static inline bool test_wanted(const char *name) {
	const char *only = getenv("TEST_ONLY");
	if (!only || !*only) {
		return true;
	}
	for (const char *w = only; *w;) {
		size_t n = strcspn(w, ",");
		for (const char *p = name; n && *p; ++p) {
			if (strncmp(p, w, n) == 0) {
				return true;
			}
		}
		w += n + (w[n] == ',');
	}
	return false;
}

#define RUN(test) do { \
	if (test_wanted(#test)) { \
		int before_ = test_failures; \
		test(); \
		printf("%s %s\n", test_failures == before_ ? "ok  " : "FAIL", #test); \
	} \
} while (0)

static inline int tests_done(void) {
	if (test_failures) {
		fprintf(stderr, "%d check(s) failed\n", test_failures);
	}
	return test_failures ? 1 : 0;
}

#endif
