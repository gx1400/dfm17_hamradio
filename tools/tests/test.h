/*
 * test.h - a minimal test harness: CHECK* macros count failures and print
 * them, TEST_MAIN runs the listed test functions and sets the exit status.
 */
#ifndef TEST_H_
#define TEST_H_

#include <stdio.h>
#include <string.h>

static int test_failures;
static int test_checks;

#define CHECK(cond) do { \
	test_checks++; \
	if (!(cond)) { \
		test_failures++; \
		printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

#define CHECK_EQ(a, b) do { \
	long long a_ = (long long)(a), b_ = (long long)(b); \
	test_checks++; \
	if (a_ != b_) { \
		test_failures++; \
		printf("  FAIL %s:%d: %s == %s (%lld != %lld)\n", \
				__FILE__, __LINE__, #a, #b, a_, b_); \
	} \
} while (0)

#define CHECK_NEAR(a, b, tol) do { \
	double a_ = (double)(a), b_ = (double)(b); \
	test_checks++; \
	if (a_ - b_ > (tol) || b_ - a_ > (tol)) { \
		test_failures++; \
		printf("  FAIL %s:%d: %s ~ %s (%g vs %g, tolerance %g)\n", \
				__FILE__, __LINE__, #a, #b, a_, b_, (double)(tol)); \
	} \
} while (0)

#define CHECK_STR(a, b) do { \
	const char *a_ = (a), *b_ = (b); \
	test_checks++; \
	if (strcmp(a_, b_) != 0) { \
		test_failures++; \
		printf("  FAIL %s:%d: %s == %s (\"%s\" != \"%s\")\n", \
				__FILE__, __LINE__, #a, #b, a_, b_); \
	} \
} while (0)

#define CHECK_MEM(a, b, n) do { \
	test_checks++; \
	if (memcmp((a), (b), (n)) != 0) { \
		test_failures++; \
		printf("  FAIL %s:%d: %s == %s (%zu bytes differ)\n", \
				__FILE__, __LINE__, #a, #b, (size_t)(n)); \
	} \
} while (0)

typedef struct { const char *name; void (*fn)(void); } test_case;

/* optional: a test file can define test_setup(), run before every test */
void test_setup(void) __attribute__((weak));

#define TEST(fn) { #fn, fn }

static inline int test_run(const char *suite, const test_case *tests, size_t n) {
	/* line-buffered, so a test that crashes (sanitizer trap) is named */
	setvbuf(stdout, NULL, _IOLBF, 0);
	for (size_t i = 0; i < n; i++) {
		int before = test_failures;
		printf("---- %s/%s\n", suite, tests[i].name);
		if (test_setup)
			test_setup();
		tests[i].fn();
		printf("%s %s/%s\n", test_failures == before ? "ok  " : "FAIL",
				suite, tests[i].name);
	}
	printf("%s: %d checks, %d failed\n", suite, test_checks, test_failures);
	return test_failures != 0;
}

#define TEST_MAIN(suite, ...) \
	int main(void) { \
		static const test_case tests_[] = { __VA_ARGS__ }; \
		return test_run(suite, tests_, sizeof(tests_) / sizeof(tests_[0])); \
	}

#endif /* TEST_H_ */
