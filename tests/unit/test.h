/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * test.h - a tiny dependency-free unit test harness
 *
 * Usage:
 *
 *	#include "test.h"
 *
 *	TEST(adds_numbers)
 *	{
 *		CHECK_INT(1 + 1, 2);
 *	}
 *
 *	TEST_MAIN(TEST_CASE(adds_numbers))
 *
 * Every file under tests/unit/ named test_*.c becomes one executable
 * and one CTest test. A failing CHECK records the failure and keeps
 * going; a failing REQUIRE also leaves the current test.
 */
#ifndef ARG_TEST_H
#define ARG_TEST_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct test_state {
	const char *name;
	unsigned checks;
	unsigned failures;
};

struct test_case {
	const char *name;
	void (*fn)(struct test_state *t_);
};

#define TEST(name) static void name(struct test_state *t_)

#if defined(__GNUC__) || defined(__clang__)
#define TEST_PRINTF_(f, a) __attribute__((format(printf, f, a)))
#else
#define TEST_PRINTF_(f, a)
#endif

static void test_fail_(struct test_state *t, const char *file, int line,
		       const char *fmt, ...) TEST_PRINTF_(4, 5);
#define TEST_CASE(fn) { #fn, fn }

static void test_fail_(struct test_state *t, const char *file, int line,
		       const char *fmt, ...)
{
	va_list ap;

	t->failures++;
	fprintf(stderr, "%s:%d: %s: ", file, line, t->name);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

#define TEST_FAIL_(...) test_fail_(t_, __FILE__, __LINE__, __VA_ARGS__)

#define CHECK(cond)                                                          \
	do {                                                                 \
		t_->checks++;                                                \
		if (!(cond))                                                 \
			TEST_FAIL_("CHECK(%s) failed", #cond);               \
	} while (0)

#define REQUIRE(cond)                                                        \
	do {                                                                 \
		t_->checks++;                                                \
		if (!(cond)) {                                               \
			TEST_FAIL_("REQUIRE(%s) failed", #cond);             \
			return;                                              \
		}                                                            \
	} while (0)

#define CHECK_INT(a, b)                                                      \
	do {                                                                 \
		long long a_ = (long long)(a), b_ = (long long)(b);          \
		t_->checks++;                                                \
		if (a_ != b_)                                                \
			TEST_FAIL_("%s == %s: %lld != %lld", #a, #b, a_, b_); \
	} while (0)

#define CHECK_UINT(a, b)                                                     \
	do {                                                                 \
		unsigned long long a_ = (unsigned long long)(a);             \
		unsigned long long b_ = (unsigned long long)(b);             \
		t_->checks++;                                                \
		if (a_ != b_)                                                \
			TEST_FAIL_("%s == %s: %llu != %llu", #a, #b, a_, b_); \
	} while (0)

#define CHECK_DBL(a, b, eps)                                                 \
	do {                                                                 \
		double a_ = (double)(a), b_ = (double)(b);                   \
		double d_ = a_ > b_ ? a_ - b_ : b_ - a_;                     \
		t_->checks++;                                                \
		if (!(d_ <= (double)(eps)))                                  \
			TEST_FAIL_("%s == %s: %g != %g", #a, #b, a_, b_);    \
	} while (0)

/* NULL-safe string comparison. */
#define CHECK_STR(a, b)                                                      \
	do {                                                                 \
		const char *a_ = (a), *b_ = (b);                             \
		t_->checks++;                                                \
		if (!((a_ == NULL && b_ == NULL) ||                          \
		      (a_ != NULL && b_ != NULL && strcmp(a_, b_) == 0)))    \
			TEST_FAIL_("%s == %s: \"%s\" != \"%s\"", #a, #b,     \
				   a_ ? a_ : "(null)", b_ ? b_ : "(null)");  \
	} while (0)

/* Compare n bytes of a (not necessarily terminated) with string b. */
#define CHECK_STRN(a, n, b)                                                  \
	do {                                                                 \
		const char *a_ = (a), *b_ = (b);                             \
		size_t n_ = (size_t)(n);                                     \
		t_->checks++;                                                \
		if (a_ == NULL || strlen(b_) != n_ || memcmp(a_, b_, n_))    \
			TEST_FAIL_("%s == %s: \"%.*s\" != \"%s\"", #a, #b,   \
				   a_ ? (int)n_ : 6, a_ ? a_ : "(null)",     \
				   b_);                                      \
	} while (0)

/* ------------------------------------------------------------------ */
/* Output capture for write hooks                                     */
/* ------------------------------------------------------------------ */

struct test_buf {
	char data[16384];
	size_t len;
	bool overflow;
};

/* Matches arg_write_fn; ud is a struct test_buf. Always NUL-terminated. */
static int test_buf_write(void *ud, const char *s, size_t n)
{
	struct test_buf *b = ud;

	if (n >= sizeof(b->data) - b->len) {
		b->overflow = true;
		return -1;
	}
	memcpy(b->data + b->len, s, n);
	b->len += n;
	b->data[b->len] = '\0';
	return 0;
}

static void test_buf_reset(struct test_buf *b)
{
	b->len = 0;
	b->overflow = false;
	b->data[0] = '\0';
}

/* ------------------------------------------------------------------ */
/* Runner                                                             */
/* ------------------------------------------------------------------ */

static int test_run_(const char *file, const struct test_case *cases,
		     size_t n)
{
	unsigned failed = 0, checks = 0;
	size_t i;

	(void)test_buf_write;
	(void)test_buf_reset;
	for (i = 0; i < n; i++) {
		struct test_state t = { cases[i].name, 0, 0 };

		cases[i].fn(&t);
		checks += t.checks;
		if (t.failures) {
			failed++;
			printf("  FAIL  %s (%u of %u checks)\n", t.name,
			       t.failures, t.checks);
		} else {
			printf("  ok    %s\n", t.name);
		}
	}
	printf("%s: %zu tests, %u checks, %u failed\n", file, n, checks,
	       failed);
	return failed ? 1 : 0;
}

#define TEST_MAIN(...)                                                       \
	int main(void)                                                       \
	{                                                                    \
		static const struct test_case cases_[] = { __VA_ARGS__ };    \
		return test_run_(__FILE__, cases_,                           \
				 sizeof(cases_) / sizeof(cases_[0]));        \
	}

#endif /* ARG_TEST_H */
