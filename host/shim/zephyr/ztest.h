/*
 * SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * The subset of the Zephyr ztest API the test suites use, for running them
 * as plain POSIX programs.
 */

#ifndef ZTEST_SHIM_H_
#define ZTEST_SHIM_H_

#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ARG_UNUSED
#define ARG_UNUSED(x) (void)(x)
#endif

struct ztest_shim_case {
	const char *name;
	void (*fn)(void);
	struct ztest_shim_case *next;
};

void ztest_shim_register(struct ztest_shim_case *tc);
void ztest_shim_suite(const char *name, void *(*setup)(void), void (*before)(void *fixture),
		      void (*after)(void *fixture), void (*teardown)(void *fixture));
void ztest_shim_fail(const char *file, int line, const char *expr, const char *fmt, ...)
	__attribute__((noreturn, format(printf, 4, 5)));

#define ZTEST(suite, name)                                                                         \
	static void suite##_##name(void);                                                          \
	static struct ztest_shim_case ztest_case_##suite##_##name = {#name, suite##_##name, NULL}; \
	__attribute__((constructor)) static void ztest_reg_##suite##_##name(void)                  \
	{                                                                                          \
		ztest_shim_register(&ztest_case_##suite##_##name);                                 \
	}                                                                                          \
	static void suite##_##name(void)

#define ZTEST_SUITE(name, pred, setup, before, after, teardown)                                    \
	__attribute__((constructor)) static void ztest_suite_##name(void)                          \
	{                                                                                          \
		ztest_shim_suite(#name, setup, before, after, teardown);                           \
	}

#define zassert_true(cond, ...)                                                                    \
	do {                                                                                       \
		if (!(cond)) {                                                                     \
			ztest_shim_fail(__FILE__, __LINE__, #cond " is false", "" __VA_ARGS__);    \
		}                                                                                  \
	} while (0)

#define zassert_false(cond, ...)                                                                   \
	do {                                                                                       \
		if (cond) {                                                                        \
			ztest_shim_fail(__FILE__, __LINE__, #cond " is true", "" __VA_ARGS__);     \
		}                                                                                  \
	} while (0)

#define zassert_equal(a, b, ...)                                                                   \
	do {                                                                                       \
		if ((a) != (b)) {                                                                  \
			ztest_shim_fail(__FILE__, __LINE__, #a " not equal to " #b,                \
					"" __VA_ARGS__);                                           \
		}                                                                                  \
	} while (0)

#define zassert_equal_ptr(a, b, ...)                                                               \
	do {                                                                                       \
		if ((const void *)(a) != (const void *)(b)) {                                      \
			ztest_shim_fail(__FILE__, __LINE__, #a " not equal to " #b,                \
					"" __VA_ARGS__);                                           \
		}                                                                                  \
	} while (0)

#define zassert_not_null(ptr, ...)                                                                 \
	do {                                                                                       \
		if ((ptr) == NULL) {                                                               \
			ztest_shim_fail(__FILE__, __LINE__, #ptr " is NULL", "" __VA_ARGS__);      \
		}                                                                                  \
	} while (0)

#define zassert_mem_equal(a, b, n, ...)                                                            \
	do {                                                                                       \
		if (memcmp((a), (b), (n)) != 0) {                                                  \
			ztest_shim_fail(__FILE__, __LINE__, #a " differs from " #b,                \
					"" __VA_ARGS__);                                           \
		}                                                                                  \
	} while (0)

#ifdef __cplusplus
}
#endif

#endif /* ZTEST_SHIM_H_ */
