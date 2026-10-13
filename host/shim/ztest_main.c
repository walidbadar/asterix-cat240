/*
 * SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <zephyr/ztest.h>

static struct ztest_shim_case *cases;
static struct ztest_shim_case **cases_tail = &cases;
static const char *suite_name = "test";
static void *(*suite_setup)(void);
static void (*suite_before)(void *);
static void (*suite_after)(void *);
static void (*suite_teardown)(void *);
static jmp_buf failure;

void ztest_shim_register(struct ztest_shim_case *tc)
{
	*cases_tail = tc;
	cases_tail = &tc->next;
}

void ztest_shim_suite(const char *name, void *(*setup)(void), void (*before)(void *),
		      void (*after)(void *), void (*teardown)(void *))
{
	suite_name = name;
	suite_setup = setup;
	suite_before = before;
	suite_after = after;
	suite_teardown = teardown;
}

void ztest_shim_fail(const char *file, int line, const char *expr, const char *fmt, ...)
{
	va_list ap;

	printf("    Assertion failed at %s:%d: %s\n", file, line, expr);
	if (fmt[0] != '\0') {
		printf("    ");
		va_start(ap, fmt);
		vprintf(fmt, ap);
		va_end(ap);
		printf("\n");
	}

	longjmp(failure, 1);
}

int main(void)
{
	void *fixture = NULL;
	unsigned int passed = 0;
	unsigned int failed = 0;

	printf("Running test suite %s\n", suite_name);

	if (suite_setup != NULL) {
		if (setjmp(failure) != 0) {
			printf("SUITE FAIL - setup failed\n");
			return EXIT_FAILURE;
		}
		fixture = suite_setup();
	}

	for (struct ztest_shim_case *tc = cases; tc != NULL; tc = tc->next) {
		printf("START - %s\n", tc->name);

		if (setjmp(failure) == 0) {
			if (suite_before != NULL) {
				suite_before(fixture);
			}
			tc->fn();
			printf(" PASS - %s\n", tc->name);
			passed++;
		} else {
			printf(" FAIL - %s\n", tc->name);
			failed++;
		}

		if (suite_after != NULL) {
			suite_after(fixture);
		}
	}

	if (suite_teardown != NULL) {
		suite_teardown(fixture);
	}

	printf("SUITE %s - [%s]: pass = %u, fail = %u\n", (failed == 0U) ? "PASS" : "FAIL",
	       suite_name, passed, failed);

	return (failed == 0U) ? EXIT_SUCCESS : EXIT_FAILURE;
}
