// SPDX-License-Identifier: GPL-2.0-only
/*
 * test_types.c - built-in value types, decorators and helpers
 */
#include "arg_internal.h"
#include "test.h"

#include <float.h>
#include <locale.h>

/*
 * Parse text through t->parse with input that is NOT NUL-terminated:
 * the bytes after the text are digits and suffix characters that
 * would change the result if the converter read past n. On failure
 * the destination must keep its 0xA5 fill.
 */
struct res {
	arg_status st;
	const char *detail;
	unsigned char dst[16];
	bool untouched;
};

static struct res run(const arg_type *t, const char *text)
{
	char buf[512];
	size_t n = strlen(text), i;
	struct res r;
	arg_ctx ctx = { NULL, NULL, NULL, ARG_E_BAD_VALUE };

	memcpy(buf, text, n);
	memcpy(buf + n, "19_kiBms.e5x", 12);
	memset(r.dst, 0xA5, sizeof(r.dst));
	r.detail = NULL;
	r.st = t->parse(t, buf, n, r.dst, &ctx) == 0 ? ARG_OK : ctx.status;
	if (r.st != ARG_OK)
		r.detail = ctx.detail;
	r.untouched = true;
	for (i = 0; i < sizeof(r.dst); i++) {
		if (r.dst[i] != 0xA5)
			r.untouched = false;
	}
	return r;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

TEST(hexval)
{
	CHECK_INT(arg_hexval('0'), 0);
	CHECK_INT(arg_hexval('9'), 9);
	CHECK_INT(arg_hexval('a'), 10);
	CHECK_INT(arg_hexval('f'), 15);
	CHECK_INT(arg_hexval('A'), 10);
	CHECK_INT(arg_hexval('F'), 15);
	CHECK_INT(arg_hexval('g'), -1);
	CHECK_INT(arg_hexval('G'), -1);
	CHECK_INT(arg_hexval('/'), -1);
	CHECK_INT(arg_hexval(':'), -1);
	CHECK_INT(arg_hexval('@'), -1);
	CHECK_INT(arg_hexval('`'), -1);
	CHECK_INT(arg_hexval('\0'), -1);
	CHECK_INT(arg_hexval((char)0xC3), -1);
}

TEST(ieq)
{
	CHECK(arg__ieq("TRUE", 4, "true"));
	CHECK(arg__ieq("tRuE", 4, "TrUe"));
	CHECK(arg__ieq("", 0, ""));
	CHECK(arg__ieq("truex", 4, "true"));
	CHECK(!arg__ieq("tru", 3, "true"));
	CHECK(!arg__ieq("truee", 5, "true"));
	CHECK(!arg__ieq("trUx", 4, "true"));
	CHECK(!arg__ieq("", 0, "a"));
	CHECK(!arg__ieq("[", 1, "{")); /* not letters: no folding */
	CHECK(!arg__ieq("@", 1, "`"));
}

TEST(reject_sets_context)
{
	arg_ctx ctx = { NULL, NULL, NULL, ARG_OK };

	CHECK_INT(arg_reject(&ctx, "bad"), -1);
	CHECK_STR(ctx.detail, "bad");
	CHECK_INT(ctx.status, ARG_E_BAD_VALUE);
	CHECK_INT(arg_reject_status(&ctx, ARG_E_RANGE, "big"), -1);
	CHECK_STR(ctx.detail, "big");
	CHECK_INT(ctx.status, ARG_E_RANGE);
	CHECK_INT(arg_reject(NULL, "x"), -1);
}

static int lazy_parse(const arg_type *t, const char *s, size_t n, void *dst,
		      arg_ctx *ctx)
{
	(void)t;
	(void)s;
	(void)n;
	(void)dst;
	ctx->status = ARG_OK; /* forgets to call arg_reject */
	return -1;
}

TEST(type_parse_entry)
{
	static const arg_type lazy = { .name = "X", .size = 1,
				       .parse = lazy_parse };
	static const arg_type noparse = { .name = "X", .size = 1 };
	const char *detail = "unset";
	int v = 0;

	CHECK_INT(arg_type_parse(&arg_t_int, "42", &v, &detail), ARG_OK);
	CHECK_INT(v, 42);
	CHECK(detail == NULL);
	CHECK_INT(arg_type_parse(&arg_t_int, "42", &v, NULL), ARG_OK);
	CHECK_INT(arg_type_parse(&arg_t_int, "x", &v, &detail),
		  ARG_E_BAD_VALUE);
	CHECK_STR(detail, "expected an integer");
	CHECK_INT(arg_type_parse(&arg_t_int, "x", &v, NULL), ARG_E_BAD_VALUE);
	CHECK_INT(arg_type_parse(&arg_t_i8, "300", &v, &detail), ARG_E_RANGE);
	CHECK_STR(detail, "out of range");
	CHECK_INT(arg_type_parse(NULL, "1", &v, &detail), ARG_E_BAD_VALUE);
	CHECK(detail != NULL);
	CHECK_INT(arg_type_parse(&arg_t_int, NULL, &v, &detail),
		  ARG_E_BAD_VALUE);
	CHECK_INT(arg_type_parse(&arg_t_int, NULL, &v, NULL),
		  ARG_E_BAD_VALUE);
	CHECK_INT(arg_type_parse(&noparse, "1", &v, NULL), ARG_E_BAD_VALUE);
	CHECK_INT(arg_type_parse(&lazy, "1", &v, NULL), ARG_E_BAD_VALUE);
	/* enum needs an item */
	CHECK_INT(arg_type_parse(&arg_t_enum, "a", &v, &detail),
		  ARG_E_BAD_VALUE);
	CHECK_STR(detail, "no choices");
}

TEST(type_metadata)
{
	static const struct {
		const arg_type *t;
		const char *name;
		size_t size;
		arg_kind kind;
	} tab[] = {
		{ &arg_t_bool, "BOOL", sizeof(bool), ARG_K_BOOL },
		{ &arg_t_i8, "N", 1, ARG_K_INT },
		{ &arg_t_i16, "N", 2, ARG_K_INT },
		{ &arg_t_i32, "N", 4, ARG_K_INT },
		{ &arg_t_i64, "N", 8, ARG_K_INT },
		{ &arg_t_int, "N", sizeof(int), ARG_K_INT },
		{ &arg_t_long, "N", sizeof(long), ARG_K_INT },
		{ &arg_t_u8, "N", 1, ARG_K_UINT },
		{ &arg_t_u16, "N", 2, ARG_K_UINT },
		{ &arg_t_u32, "N", 4, ARG_K_UINT },
		{ &arg_t_u64, "N", 8, ARG_K_UINT },
		{ &arg_t_uint, "N", sizeof(unsigned), ARG_K_UINT },
		{ &arg_t_size_t, "N", sizeof(size_t), ARG_K_UINT },
		{ &arg_t_double, "NUM", sizeof(double), ARG_K_DOUBLE },
		{ &arg_t_float, "NUM", sizeof(float), ARG_K_DOUBLE },
		{ &arg_t_string, "TEXT", sizeof(const char *), ARG_K_STR },
		{ &arg_t_enum, "CHOICE", sizeof(int), ARG_K_ENUM },
		{ &arg_t_bytesize, "SIZE", 8, ARG_K_UINT },
		{ &arg_t_duration, "DURATION", 8, ARG_K_UINT },
	};
	size_t i;

	for (i = 0; i < ARG__COUNTOF(tab); i++) {
		CHECK_STR(tab[i].t->name, tab[i].name);
		CHECK_UINT(tab[i].t->size, tab[i].size);
		CHECK_INT(tab[i].t->kind, tab[i].kind);
		CHECK(tab[i].t->parse != NULL);
		CHECK(tab[i].t->format != NULL);
		CHECK_UINT(tab[i].t->hints, 0);
	}
}

/* ------------------------------------------------------------------ */
/* bool                                                               */
/* ------------------------------------------------------------------ */

TEST(bool_values)
{
	static const struct {
		const char *in;
		int want; /* 1, 0, -1 = error */
	} tab[] = {
		{ "true", 1 },  { "TRUE", 1 },   { "True", 1 },  { "yes", 1 },
		{ "YeS", 1 },   { "on", 1 },     { "ON", 1 },    { "1", 1 },
		{ "false", 0 }, { "FALSE", 0 },  { "no", 0 },    { "NO", 0 },
		{ "off", 0 },   { "Off", 0 },    { "0", 0 },     { "", -1 },
		{ "2", -1 },    { "tru", -1 },   { "truee", -1 }, { "y", -1 },
		{ "n", -1 },    { " true", -1 }, { "true ", -1 }, { "01", -1 },
		{ "of", -1 },   { "-1", -1 },
	};
	size_t i;

	for (i = 0; i < ARG__COUNTOF(tab); i++) {
		struct res r = run(&arg_t_bool, tab[i].in);
		bool v;

		if (tab[i].want < 0) {
			CHECK_INT(r.st, ARG_E_BAD_VALUE);
			CHECK(r.untouched);
			CHECK(r.detail != NULL);
			continue;
		}
		CHECK_INT(r.st, ARG_OK);
		memcpy(&v, r.dst, sizeof(v));
		if (v != (tab[i].want == 1))
			TEST_FAIL_("bool \"%s\"", tab[i].in);
	}
}

TEST(bool_format)
{
	char buf[8];
	bool v = true;

	CHECK_INT(arg_t_bool.format(&arg_t_bool, &v, buf, sizeof(buf)), 4);
	CHECK_STR(buf, "true");
	v = false;
	CHECK_INT(arg_t_bool.format(&arg_t_bool, &v, buf, sizeof(buf)), 5);
	CHECK_STR(buf, "false");
	/* truncation keeps the full length and terminates */
	CHECK_INT(arg_t_bool.format(&arg_t_bool, &v, buf, 3), 5);
	CHECK_STR(buf, "fa");
	CHECK_INT(arg_t_bool.format(&arg_t_bool, &v, buf, 1), 5);
	CHECK_STR(buf, "");
	CHECK_INT(arg_t_bool.format(&arg_t_bool, &v, NULL, 0), 5);
}

/* ------------------------------------------------------------------ */
/* Integers                                                           */
/* ------------------------------------------------------------------ */

#define OK ARG_OK
#define BAD ARG_E_BAD_VALUE
#define RNG ARG_E_RANGE

struct int_case {
	const arg_type *t;
	const char *in;
	arg_status st;
	int64_t want;
};

TEST(signed_integers)
{
	static const struct int_case tab[] = {
		/* plain decimal */
		{ &arg_t_i64, "0", OK, 0 },
		{ &arg_t_i64, "7", OK, 7 },
		{ &arg_t_i64, "007", OK, 7 },
		{ &arg_t_i64, "+42", OK, 42 },
		{ &arg_t_i64, "-42", OK, -42 },
		{ &arg_t_i64, "-0", OK, 0 },
		{ &arg_t_i64, "+0", OK, 0 },
		{ &arg_t_i64, "1234567890", OK, 1234567890 },
		/* prefixes */
		{ &arg_t_i64, "0x1f", OK, 31 },
		{ &arg_t_i64, "0X1F", OK, 31 },
		{ &arg_t_i64, "0xaBcD", OK, 0xabcd },
		{ &arg_t_i64, "-0x10", OK, -16 },
		{ &arg_t_i64, "+0x10", OK, 16 },
		{ &arg_t_i64, "0o17", OK, 15 },
		{ &arg_t_i64, "0O17", OK, 15 },
		{ &arg_t_i64, "0b101", OK, 5 },
		{ &arg_t_i64, "0B101", OK, 5 },
		{ &arg_t_i64, "-0b1", OK, -1 },
		{ &arg_t_i64, "0x0b", OK, 11 },
		{ &arg_t_i64, "0x0", OK, 0 },
		{ &arg_t_i64, "0b0", OK, 0 },
		{ &arg_t_i64, "00", OK, 0 },
		{ &arg_t_i64, "08", OK, 8 },
		/* separators */
		{ &arg_t_i64, "1_000", OK, 1000 },
		{ &arg_t_i64, "1_000_000", OK, 1000000 },
		{ &arg_t_i64, "0_0", OK, 0 },
		{ &arg_t_i64, "0xff_ff", OK, 0xffff },
		{ &arg_t_i64, "0b1_0", OK, 2 },
		{ &arg_t_i64, "-1_0", OK, -10 },
		{ &arg_t_i64, "_1", BAD, 0 },
		{ &arg_t_i64, "1_", BAD, 0 },
		{ &arg_t_i64, "1__0", BAD, 0 },
		{ &arg_t_i64, "0x_1", BAD, 0 },
		{ &arg_t_i64, "0_x1", BAD, 0 },
		{ &arg_t_i64, "-_1", BAD, 0 },
		{ &arg_t_i64, "_", BAD, 0 },
		/* garbage */
		{ &arg_t_i64, "", BAD, 0 },
		{ &arg_t_i64, "+", BAD, 0 },
		{ &arg_t_i64, "-", BAD, 0 },
		{ &arg_t_i64, "--1", BAD, 0 },
		{ &arg_t_i64, "+-1", BAD, 0 },
		{ &arg_t_i64, " 1", BAD, 0 },
		{ &arg_t_i64, "1 ", BAD, 0 },
		{ &arg_t_i64, "1a", BAD, 0 },
		{ &arg_t_i64, "a", BAD, 0 },
		{ &arg_t_i64, "1.0", BAD, 0 },
		{ &arg_t_i64, "1e3", BAD, 0 },
		{ &arg_t_i64, "0x", BAD, 0 },
		{ &arg_t_i64, "0o", BAD, 0 },
		{ &arg_t_i64, "0b", BAD, 0 },
		{ &arg_t_i64, "0xg", BAD, 0 },
		{ &arg_t_i64, "0o8", BAD, 0 },
		{ &arg_t_i64, "0b2", BAD, 0 },
		{ &arg_t_i64, "0z1", BAD, 0 },
		{ &arg_t_i64, "12abc", BAD, 0 },
		{ &arg_t_i64, "0x-1", BAD, 0 },
		/* garbage after an overflowing number is still garbage */
		{ &arg_t_i64, "99999999999999999999x", BAD, 0 },
		/* int8 */
		{ &arg_t_i8, "127", OK, 127 },
		{ &arg_t_i8, "-128", OK, -128 },
		{ &arg_t_i8, "128", RNG, 0 },
		{ &arg_t_i8, "-129", RNG, 0 },
		{ &arg_t_i8, "0x7f", OK, 127 },
		{ &arg_t_i8, "0x80", RNG, 0 },
		{ &arg_t_i8, "-0x80", OK, -128 },
		/* int16 */
		{ &arg_t_i16, "32767", OK, INT16_MAX },
		{ &arg_t_i16, "-32768", OK, INT16_MIN },
		{ &arg_t_i16, "32768", RNG, 0 },
		{ &arg_t_i16, "-32769", RNG, 0 },
		/* int32 */
		{ &arg_t_i32, "2147483647", OK, INT32_MAX },
		{ &arg_t_i32, "-2147483648", OK, INT32_MIN },
		{ &arg_t_i32, "2147483648", RNG, 0 },
		{ &arg_t_i32, "-2147483649", RNG, 0 },
		/* int64 */
		{ &arg_t_i64, "9223372036854775807", OK, INT64_MAX },
		{ &arg_t_i64, "-9223372036854775808", OK, INT64_MIN },
		{ &arg_t_i64, "9223372036854775808", RNG, 0 },
		{ &arg_t_i64, "-9223372036854775809", RNG, 0 },
		{ &arg_t_i64, "0x7fffffffffffffff", OK, INT64_MAX },
		{ &arg_t_i64, "-0x8000000000000000", OK, INT64_MIN },
		{ &arg_t_i64, "0x8000000000000000", RNG, 0 },
		{ &arg_t_i64, "18446744073709551615", RNG, 0 },
		{ &arg_t_i64, "18446744073709551616", RNG, 0 },
		{ &arg_t_i64, "-99999999999999999999999", RNG, 0 },
		{ &arg_t_i64, "0x1_0000_0000_0000_0000", RNG, 0 },
		/* int, long */
		{ &arg_t_int, "-5", OK, -5 },
		{ &arg_t_int, "2147483647", OK, 2147483647 },
		{ &arg_t_long, "-123456", OK, -123456 },
		{ &arg_t_long, "2147483647", OK, 2147483647 },
	};
	size_t i;

	for (i = 0; i < ARG__COUNTOF(tab); i++) {
		struct res r = run(tab[i].t, tab[i].in);
		int64_t v = 0;

		if (r.st != tab[i].st) {
			TEST_FAIL_("\"%s\": status %d, want %d", tab[i].in,
				   (int)r.st, (int)tab[i].st);
			continue;
		}
		t_->checks++;
		if (r.st != ARG_OK) {
			CHECK(r.untouched);
			CHECK_STR(r.detail, r.st == RNG
						    ? "out of range"
						    : "expected an integer");
			continue;
		}
		CHECK(arg__load_int(tab[i].t, r.dst, &v));
		if (v != tab[i].want)
			TEST_FAIL_("\"%s\": %lld, want %lld", tab[i].in,
				   (long long)v, (long long)tab[i].want);
		/* only the type's own bytes are written */
		if (tab[i].t->size < sizeof(r.dst))
			CHECK_INT(r.dst[tab[i].t->size], 0xA5);
	}
}

struct uint_case {
	const arg_type *t;
	const char *in;
	arg_status st;
	uint64_t want;
	const char *detail;
};

TEST(unsigned_integers)
{
	static const char *const neg = "expected a non-negative integer";
	static const char *const bad = "expected an integer";
	static const char *const rng = "out of range";
	static const struct uint_case tab[] = {
		{ &arg_t_u64, "0", OK, 0, NULL },
		{ &arg_t_u64, "+0", OK, 0, NULL },
		{ &arg_t_u64, "+17", OK, 17, NULL },
		{ &arg_t_u64, "0xFF", OK, 255, NULL },
		{ &arg_t_u64, "0o777", OK, 511, NULL },
		{ &arg_t_u64, "0b1111_0000", OK, 240, NULL },
		{ &arg_t_u64, "1_2_3", OK, 123, NULL },
		{ &arg_t_u64, "-0", BAD, 0, neg },
		{ &arg_t_u64, "-1", BAD, 0, neg },
		{ &arg_t_u64, "-", BAD, 0, neg },
		{ &arg_t_u64, "-x", BAD, 0, neg },
		{ &arg_t_u64, "", BAD, 0, bad },
		{ &arg_t_u64, "+", BAD, 0, bad },
		{ &arg_t_u64, "+-1", BAD, 0, bad },
		{ &arg_t_u64, "1_", BAD, 0, bad },
		{ &arg_t_u64, "abc", BAD, 0, bad },
		{ &arg_t_u64, "18446744073709551615", OK, UINT64_MAX, NULL },
		{ &arg_t_u64, "0xffffffffffffffff", OK, UINT64_MAX, NULL },
		{ &arg_t_u64, "18446744073709551616", RNG, 0, rng },
		{ &arg_t_u64, "0x1_0000_0000_0000_0000", RNG, 0, rng },
		{ &arg_t_u64, "0b1" "0000000000000000000000000000000000000000"
			      "000000000000000000000000", RNG, 0, rng },
		{ &arg_t_u8, "255", OK, 255, NULL },
		{ &arg_t_u8, "256", RNG, 0, rng },
		{ &arg_t_u8, "0xff", OK, 255, NULL },
		{ &arg_t_u8, "0x100", RNG, 0, rng },
		{ &arg_t_u16, "65535", OK, 65535, NULL },
		{ &arg_t_u16, "65536", RNG, 0, rng },
		{ &arg_t_u32, "4294967295", OK, UINT32_MAX, NULL },
		{ &arg_t_u32, "4294967296", RNG, 0, rng },
		{ &arg_t_uint, "4294967295", OK, UINT32_MAX, NULL },
		{ &arg_t_uint, "-1", BAD, 0, neg },
		{ &arg_t_size_t, "4096", OK, 4096, NULL },
		{ &arg_t_size_t, "-4096", BAD, 0, neg },
	};
	size_t i;

	for (i = 0; i < ARG__COUNTOF(tab); i++) {
		struct res r = run(tab[i].t, tab[i].in);
		uint64_t v = 0;

		if (r.st != tab[i].st) {
			TEST_FAIL_("\"%s\": status %d, want %d", tab[i].in,
				   (int)r.st, (int)tab[i].st);
			continue;
		}
		t_->checks++;
		if (r.st != ARG_OK) {
			CHECK(r.untouched);
			CHECK_STR(r.detail, tab[i].detail);
			continue;
		}
		CHECK(arg__load_uint(tab[i].t, r.dst, &v));
		if (v != tab[i].want)
			TEST_FAIL_("\"%s\": %llu, want %llu", tab[i].in,
				   (unsigned long long)v,
				   (unsigned long long)tab[i].want);
		if (tab[i].t->size < sizeof(r.dst))
			CHECK_INT(r.dst[tab[i].t->size], 0xA5);
	}
}

TEST(integer_unsupported_size)
{
	static const arg_type i3 = { .name = "N", .size = 3,
				     .kind = ARG_K_INT };
	arg_type si = arg_t_i64, su = arg_t_u64;
	struct res r;

	si.size = 3;
	su.size = 16;
	r = run(&si, "1");
	CHECK_INT(r.st, ARG_E_BAD_VALUE);
	CHECK(r.untouched);
	r = run(&su, "1");
	CHECK_INT(r.st, ARG_E_BAD_VALUE);
	CHECK(r.untouched);
	/* format refuses too */
	CHECK_INT(arg_t_i64.format(&i3, r.dst, NULL, 0), -1);
	CHECK_INT(arg_t_u64.format(&i3, r.dst, NULL, 0), -1);
}

TEST(integer_short_input_window)
{
	/* only n bytes may be read: "12" out of "123" */
	const char *s = "123";
	arg_ctx ctx = { NULL, NULL, NULL, ARG_E_BAD_VALUE };
	int64_t v = -1;
	uint8_t u = 0;

	CHECK_INT(arg_t_i64.parse(&arg_t_i64, s, 2, &v, &ctx), 0);
	CHECK_INT(v, 12);
	CHECK_INT(arg_t_i64.parse(&arg_t_i64, "0x", 1, &v, &ctx), 0);
	CHECK_INT(v, 0);
	CHECK_INT(arg_t_u8.parse(&arg_t_u8, "2559", 3, &u, &ctx), 0);
	CHECK_INT(u, 255);
	CHECK_INT(arg_t_i64.parse(&arg_t_i64, "1_2", 2, &v, &ctx), -1);
	CHECK_INT(arg_t_i64.parse(&arg_t_i64, "-5", 1, &v, &ctx), -1);
}

TEST(integer_format)
{
	static const struct {
		const arg_type *t;
		int64_t v;
		const char *want;
	} stab[] = {
		{ &arg_t_i8, 0, "0" },
		{ &arg_t_i8, -128, "-128" },
		{ &arg_t_i8, 127, "127" },
		{ &arg_t_i16, -32768, "-32768" },
		{ &arg_t_i32, INT32_MIN, "-2147483648" },
		{ &arg_t_i64, INT64_MIN, "-9223372036854775808" },
		{ &arg_t_i64, INT64_MAX, "9223372036854775807" },
		{ &arg_t_i64, -1, "-1" },
		{ &arg_t_int, 42, "42" },
		{ &arg_t_long, -7, "-7" },
	};
	char buf[32];
	unsigned char raw[8];
	size_t i;

	for (i = 0; i < ARG__COUNTOF(stab); i++) {
		int64_t back = 0;
		int len;

		switch (stab[i].t->size) {
		case 1: {
			int8_t x = (int8_t)stab[i].v;

			memcpy(raw, &x, 1);
			break;
		}
		case 2: {
			int16_t x = (int16_t)stab[i].v;

			memcpy(raw, &x, 2);
			break;
		}
		case 4: {
			int32_t x = (int32_t)stab[i].v;

			memcpy(raw, &x, 4);
			break;
		}
		default:
			memcpy(raw, &stab[i].v, 8);
			break;
		}
		len = stab[i].t->format(stab[i].t, raw, buf, sizeof(buf));
		CHECK_STR(buf, stab[i].want);
		CHECK_INT(len, strlen(stab[i].want));
		CHECK_INT(arg_type_parse(stab[i].t, buf, raw, NULL), ARG_OK);
		CHECK(arg__load_int(stab[i].t, raw, &back));
		CHECK_INT(back, stab[i].v);
	}

	{
		uint64_t u = UINT64_MAX;
		uint8_t u8 = 200;

		CHECK_INT(arg_t_u64.format(&arg_t_u64, &u, buf, sizeof(buf)),
			  20);
		CHECK_STR(buf, "18446744073709551615");
		CHECK_INT(arg_t_u64.format(&arg_t_u64, &u, buf, 5), 20);
		CHECK_STR(buf, "1844");
		CHECK_INT(arg_t_u8.format(&arg_t_u8, &u8, buf, sizeof(buf)),
			  3);
		CHECK_STR(buf, "200");
	}
}

/* parse(format(v)) == v over many values for every integer type */
TEST(integer_round_trip)
{
	static const arg_type *const types[] = {
		&arg_t_i8,  &arg_t_i16, &arg_t_i32,  &arg_t_i64,
		&arg_t_int, &arg_t_long, &arg_t_u8,  &arg_t_u16,
		&arg_t_u32, &arg_t_u64, &arg_t_uint, &arg_t_size_t,
	};
	uint64_t x = UINT64_C(0x9E3779B97F4A7C15);
	size_t i, k;

	for (i = 0; i < ARG__COUNTOF(types); i++) {
		const arg_type *t = types[i];

		for (k = 0; k < 500; k++) {
			unsigned char a[8], b[8];
			char buf[32];
			int len;

			x ^= x << 13;
			x ^= x >> 7;
			x ^= x << 17;
			/* also hit 0, small and extreme values */
			if (k < 3)
				memset(a, k == 0 ? 0 : (k == 1 ? 0xff : 0x80),
				       sizeof(a));
			else
				memcpy(a, &x, sizeof(a));
			if (k == 3 && t->kind == ARG_K_INT)
				a[t->size - 1] = 0x80; /* top byte */
			len = t->format(t, a, buf, sizeof(buf));
			REQUIRE(len > 0 && (size_t)len < sizeof(buf));
			memset(b, 0, sizeof(b));
			if (arg_type_parse(t, buf, b, NULL) != ARG_OK ||
			    memcmp(a, b, t->size) != 0)
				TEST_FAIL_("%s round trip of \"%s\"", t->name,
					   buf);
		}
	}
}

/* ------------------------------------------------------------------ */
/* double / float                                                     */
/* ------------------------------------------------------------------ */

#ifndef ARG_NO_FLOAT

struct dbl_case {
	const char *in;
	arg_status st;
	double want;
};

static bool is_nan(double d)
{
	return d != d;
}

TEST(doubles)
{
	static const struct dbl_case tab[] = {
		{ "0", OK, 0.0 },
		{ "1", OK, 1.0 },
		{ "-1", OK, -1.0 },
		{ "+1", OK, 1.0 },
		{ "1.5", OK, 1.5 },
		{ "1.", OK, 1.0 },
		{ ".5", OK, 0.5 },
		{ "-.5", OK, -0.5 },
		{ "1e3", OK, 1000.0 },
		{ "1E3", OK, 1000.0 },
		{ "1e+3", OK, 1000.0 },
		{ "1e-3", OK, 0.001 },
		{ "2.5e2", OK, 250.0 },
		{ ".5e1", OK, 5.0 },
		{ "5.e1", OK, 50.0 },
		{ "007.25", OK, 7.25 },
		{ "1e-400", OK, 0.0 }, /* underflow is accepted */
		{ "1.7976931348623157e308", OK, DBL_MAX },
		{ "1e309", RNG, 0 },
		{ "-1e309", RNG, 0 },
		{ "2e308", RNG, 0 },
		{ "", BAD, 0 },
		{ ".", BAD, 0 },
		{ "-", BAD, 0 },
		{ "+.", BAD, 0 },
		{ "e3", BAD, 0 },
		{ "1e", BAD, 0 },
		{ "1e+", BAD, 0 },
		{ "1e-", BAD, 0 },
		{ "1.5.5", BAD, 0 },
		{ "1,5", BAD, 0 },
		{ " 1", BAD, 0 },
		{ "1 ", BAD, 0 },
		{ "0x10", BAD, 0 },
		{ "0x1p3", BAD, 0 },
		{ "1_000", BAD, 0 },
		{ "--1", BAD, 0 },
		{ "in", BAD, 0 },
		{ "infinit", BAD, 0 },
		{ "infinityy", BAD, 0 },
		{ "nana", BAD, 0 },
		{ "nan(1)", BAD, 0 },
		{ "1f", BAD, 0 },
	};
	size_t i;

	for (i = 0; i < ARG__COUNTOF(tab); i++) {
		struct res r = run(&arg_t_double, tab[i].in);
		double d;

		if (r.st != tab[i].st) {
			TEST_FAIL_("\"%s\": status %d, want %d", tab[i].in,
				   (int)r.st, (int)tab[i].st);
			continue;
		}
		t_->checks++;
		if (r.st != ARG_OK) {
			CHECK(r.untouched);
			CHECK_STR(r.detail, r.st == RNG ? "out of range"
							: "expected a number");
			continue;
		}
		memcpy(&d, r.dst, sizeof(d));
		if (d != tab[i].want)
			TEST_FAIL_("\"%s\": %.17g, want %.17g", tab[i].in, d,
				   tab[i].want);
	}
}

TEST(doubles_special)
{
	static const char *const pinf[] = { "inf", "INF", "Infinity",
					    "+inf", "+INFINITY", "iNfInItY" };
	static const char *const ninf[] = { "-inf", "-Infinity" };
	static const char *const nans[] = { "nan", "NaN", "-nan", "+NAN" };
	size_t i;
	double d;

	for (i = 0; i < ARG__COUNTOF(pinf); i++) {
		CHECK_INT(arg_type_parse(&arg_t_double, pinf[i], &d, NULL),
			  ARG_OK);
		CHECK(d > DBL_MAX);
	}
	for (i = 0; i < ARG__COUNTOF(ninf); i++) {
		CHECK_INT(arg_type_parse(&arg_t_double, ninf[i], &d, NULL),
			  ARG_OK);
		CHECK(d < -DBL_MAX);
	}
	for (i = 0; i < ARG__COUNTOF(nans); i++) {
		CHECK_INT(arg_type_parse(&arg_t_double, nans[i], &d, NULL),
			  ARG_OK);
		CHECK(is_nan(d));
	}
}

TEST(doubles_long_input)
{
	char big[400];
	double d;

	/* 256 digits are fine, 257 are rejected (never truncated) */
	memset(big, '0', sizeof(big));
	big[0] = '1';
	big[256] = '\0';
	CHECK_INT(arg_type_parse(&arg_t_double, big, &d, NULL), ARG_OK);
	CHECK(d > 9e254 && d < 1.1e255);
	big[256] = '0';
	big[257] = '\0';
	CHECK_INT(arg_type_parse(&arg_t_double, big, &d, NULL),
		  ARG_E_BAD_VALUE);
}

TEST(doubles_short_input_window)
{
	arg_ctx ctx = { NULL, NULL, NULL, ARG_E_BAD_VALUE };
	double d = 0;
	float f = 0;

	CHECK_INT(arg_t_double.parse(&arg_t_double, "1.25e5", 4, &d, &ctx),
		  0);
	CHECK(d == 1.25);
	CHECK_INT(arg_t_double.parse(&arg_t_double, "1e5", 2, &d, &ctx), -1);
	CHECK_INT(arg_t_double.parse(&arg_t_double, "info", 3, &d, &ctx), 0);
	CHECK(d > DBL_MAX);
	CHECK_INT(arg_t_float.parse(&arg_t_float, "2.5x", 3, &f, &ctx), 0);
	CHECK(f == 2.5f);
}

TEST(floats)
{
	float f;
	const char *detail;

	CHECK_INT(arg_type_parse(&arg_t_float, "1.5", &f, NULL), ARG_OK);
	CHECK(f == 1.5f);
	CHECK_INT(arg_type_parse(&arg_t_float, "-0.25", &f, NULL), ARG_OK);
	CHECK(f == -0.25f);
	CHECK_INT(arg_type_parse(&arg_t_float, "3.4028234e38", &f, NULL),
		  ARG_OK);
	CHECK_INT(arg_type_parse(&arg_t_float, "1e39", &f, &detail),
		  ARG_E_RANGE);
	CHECK_STR(detail, "out of range");
	CHECK_INT(arg_type_parse(&arg_t_float, "-1e39", &f, NULL),
		  ARG_E_RANGE);
	CHECK_INT(arg_type_parse(&arg_t_float, "1e309", &f, NULL),
		  ARG_E_RANGE);
	CHECK_INT(arg_type_parse(&arg_t_float, "inf", &f, NULL), ARG_OK);
	CHECK(f > FLT_MAX);
	CHECK_INT(arg_type_parse(&arg_t_float, "-inf", &f, NULL), ARG_OK);
	CHECK(f < -FLT_MAX);
	CHECK_INT(arg_type_parse(&arg_t_float, "nan", &f, NULL), ARG_OK);
	CHECK(f != f);
	CHECK_INT(arg_type_parse(&arg_t_float, "x", &f, &detail),
		  ARG_E_BAD_VALUE);
	CHECK_STR(detail, "expected a number");
}

TEST(double_format)
{
	static const struct {
		double v;
		const char *want;
	} tab[] = {
		{ 0.0, "0" },     { 1.0, "1" },        { -2.5, "-2.5" },
		{ 0.1, "0.1" },   { 1e100, "1e+100" }, { 0.5, "0.5" },
		{ 1234.5, "1234.5" },
	};
	char buf[64];
	size_t i;
	double d;
	float f;

	for (i = 0; i < ARG__COUNTOF(tab); i++) {
		int len = arg_t_double.format(&arg_t_double, &tab[i].v, buf,
					      sizeof(buf));

		CHECK_STR(buf, tab[i].want);
		CHECK_INT(len, strlen(tab[i].want));
	}
	d = 1.0 / 3.0;
	arg_t_double.format(&arg_t_double, &d, buf, sizeof(buf));
	CHECK_STR(buf, "0.3333333333333333");
	f = 0.1f;
	arg_t_float.format(&arg_t_float, &f, buf, sizeof(buf));
	CHECK_STR(buf, "0.1");
	f = 1.0f / 3.0f;
	arg_t_float.format(&arg_t_float, &f, buf, sizeof(buf));
	CHECK(strlen(buf) <= 11);
	{
		float back = 0;

		CHECK_INT(arg_type_parse(&arg_t_float, buf, &back, NULL),
			  ARG_OK);
		CHECK(back == f);
	}
	CHECK_INT(arg_type_parse(&arg_t_double, "inf", &d, NULL), ARG_OK);
	arg_t_double.format(&arg_t_double, &d, buf, sizeof(buf));
	CHECK_STR(buf, "inf");
	CHECK_INT(arg_type_parse(&arg_t_double, "nan", &d, NULL), ARG_OK);
	arg_t_double.format(&arg_t_double, &d, buf, sizeof(buf));
	CHECK(arg__ieq(buf + (buf[0] == '-'), 3, "nan"));
	CHECK_INT(arg_type_parse(&arg_t_double, buf, &d, NULL), ARG_OK);
	CHECK(is_nan(d));
	/* a non-double type is refused */
	CHECK_INT(arg_t_double.format(&arg_t_i64, &d, buf, sizeof(buf)), -1);
}

TEST(double_round_trip)
{
	uint64_t x = UINT64_C(0x2545F4914F6CDD1D);
	size_t k;

	for (k = 0; k < 2000; k++) {
		double a, b;
		float fa, fb;
		char buf[64];

		x ^= x << 13;
		x ^= x >> 7;
		x ^= x << 17;
		memcpy(&a, &x, sizeof(a));
		if (is_nan(a) || a > DBL_MAX || a < -DBL_MAX)
			continue;
		arg_t_double.format(&arg_t_double, &a, buf, sizeof(buf));
		if (arg_type_parse(&arg_t_double, buf, &b, NULL) != ARG_OK ||
		    a != b)
			TEST_FAIL_("double round trip \"%s\"", buf);
		t_->checks++;

		{
			uint32_t w = (uint32_t)(x >> 32);

			memcpy(&fa, &w, sizeof(fa));
		}
		if (fa != fa || fa > FLT_MAX || fa < -FLT_MAX)
			continue;
		arg_t_float.format(&arg_t_float, &fa, buf, sizeof(buf));
		if (arg_type_parse(&arg_t_float, buf, &fb, NULL) != ARG_OK ||
		    fa != fb)
			TEST_FAIL_("float round trip \"%s\"", buf);
		t_->checks++;
	}
}

/* Use a locale with a decimal comma when the system has one. */
TEST(double_locale_independent)
{
	static const char *const names[] = { "de_DE.UTF-8", "de_DE.utf8",
					     "fr_FR.UTF-8", "de_DE",
					     "fr_FR" };
	const char *got = NULL;
	char buf[64];
	double d = 0;
	size_t i;

	for (i = 0; i < ARG__COUNTOF(names) && !got; i++)
		got = setlocale(LC_NUMERIC, names[i]);
	if (!got || localeconv()->decimal_point[0] == '.') {
		setlocale(LC_NUMERIC, "C");
		printf("  (no decimal-comma locale, check skipped)\n");
		return;
	}
	CHECK_INT(arg_type_parse(&arg_t_double, "1.5", &d, NULL), ARG_OK);
	CHECK(d == 1.5);
	CHECK_INT(arg_type_parse(&arg_t_double, "1,5", &d, NULL),
		  ARG_E_BAD_VALUE);
	d = 2.25;
	arg_t_double.format(&arg_t_double, &d, buf, sizeof(buf));
	CHECK_STR(buf, "2.25");
	setlocale(LC_NUMERIC, "C");
}

#else /* ARG_NO_FLOAT */

TEST(float_disabled)
{
	const char *detail;
	double d = 1;

	CHECK_INT(arg_type_parse(&arg_t_double, "1", &d, &detail),
		  ARG_E_BAD_VALUE);
	CHECK_STR(detail, "floating point disabled");
	CHECK_INT(arg_t_double.format(&arg_t_double, &d, NULL, 0), -1);
}

#endif /* ARG_NO_FLOAT */

/* ------------------------------------------------------------------ */
/* string                                                             */
/* ------------------------------------------------------------------ */

TEST(strings)
{
	const char *v = NULL, *in = "hello world";
	char buf[16];

	CHECK_INT(arg_type_parse(&arg_t_string, in, &v, NULL), ARG_OK);
	CHECK(v == in); /* borrowed, not copied */
	CHECK_INT(arg_type_parse(&arg_t_string, "", &v, NULL), ARG_OK);
	CHECK_STR(v, "");
	CHECK_INT(arg_type_parse(&arg_t_string, "\xff\x01-x", &v, NULL),
		  ARG_OK);
	CHECK_STR(v, "\xff\x01-x");

	v = "abc";
	CHECK_INT(arg_t_string.format(&arg_t_string, &v, buf, sizeof(buf)),
		  3);
	CHECK_STR(buf, "abc");
	CHECK_INT(arg_t_string.format(&arg_t_string, &v, buf, 2), 3);
	CHECK_STR(buf, "a");
	v = NULL;
	CHECK_INT(arg_t_string.format(&arg_t_string, &v, buf, sizeof(buf)),
		  -1);
}

/* ------------------------------------------------------------------ */
/* enum                                                               */
/* ------------------------------------------------------------------ */

TEST(enums)
{
	const arg_item item = ARG_ENUM('f', "format", "Format",
					      ARG_CHOICES("json", "yaml",
							  "toml"));
	static const arg_item bare = { .kind = ARG_I_VALUE, .name = "x" };
	arg_ctx ctx = { NULL, &item, NULL, ARG_E_BAD_VALUE };
	int v = -1;
	char buf[8];

	CHECK_INT(arg_t_enum.parse(&arg_t_enum, "json", 4, &v, &ctx), 0);
	CHECK_INT(v, 0);
	CHECK_INT(arg_t_enum.parse(&arg_t_enum, "toml", 4, &v, &ctx), 0);
	CHECK_INT(v, 2);
	/* only n bytes count: "yaml" out of "yamlx" */
	CHECK_INT(arg_t_enum.parse(&arg_t_enum, "yamlx", 4, &v, &ctx), 0);
	CHECK_INT(v, 1);
	v = 7;
	CHECK_INT(arg_t_enum.parse(&arg_t_enum, "JSON", 4, &v, &ctx), -1);
	CHECK_STR(ctx.detail, "invalid choice");
	CHECK_INT(ctx.status, ARG_E_BAD_VALUE);
	CHECK_INT(v, 7);
	CHECK_INT(arg_t_enum.parse(&arg_t_enum, "jso", 3, &v, &ctx), -1);
	CHECK_INT(arg_t_enum.parse(&arg_t_enum, "", 0, &v, &ctx), -1);
	CHECK_INT(v, 7);

	ctx.item = &bare;
	CHECK_INT(arg_t_enum.parse(&arg_t_enum, "json", 4, &v, &ctx), -1);
	CHECK_STR(ctx.detail, "no choices");
	ctx.item = NULL;
	CHECK_INT(arg_t_enum.parse(&arg_t_enum, "json", 4, &v, &ctx), -1);
	CHECK_STR(ctx.detail, "no choices");

	v = 0;
	CHECK_INT(arg_t_enum.format(&arg_t_enum, &v, buf, sizeof(buf)), -1);
	CHECK_STR(buf, "");
	CHECK_INT(arg_t_enum.format(&arg_t_enum, &v, NULL, 0), -1);
}

/* ------------------------------------------------------------------ */
/* bytesize                                                           */
/* ------------------------------------------------------------------ */

TEST(bytesizes)
{
	static const struct uint_case tab[] = {
		{ &arg_t_bytesize, "0", OK, 0, NULL },
		{ &arg_t_bytesize, "512", OK, 512, NULL },
		{ &arg_t_bytesize, "512b", OK, 512, NULL },
		{ &arg_t_bytesize, "512B", OK, 512, NULL },
		{ &arg_t_bytesize, "1_000", OK, 1000, NULL },
		{ &arg_t_bytesize, "4k", OK, 4000, NULL },
		{ &arg_t_bytesize, "4K", OK, 4000, NULL },
		{ &arg_t_bytesize, "4kb", OK, 4000, NULL },
		{ &arg_t_bytesize, "4KB", OK, 4000, NULL },
		{ &arg_t_bytesize, "16M", OK, 16000000, NULL },
		{ &arg_t_bytesize, "16mb", OK, 16000000, NULL },
		{ &arg_t_bytesize, "2G", OK, UINT64_C(2000000000), NULL },
		{ &arg_t_bytesize, "2gB", OK, UINT64_C(2000000000), NULL },
		{ &arg_t_bytesize, "3t", OK, UINT64_C(3000000000000), NULL },
		{ &arg_t_bytesize, "3TB", OK, UINT64_C(3000000000000), NULL },
		{ &arg_t_bytesize, "5p", OK, UINT64_C(5000000000000000),
		  NULL },
		{ &arg_t_bytesize, "5pb", OK, UINT64_C(5000000000000000),
		  NULL },
		{ &arg_t_bytesize, "18e", OK, UINT64_C(18000000000000000000),
		  NULL },
		{ &arg_t_bytesize, "1EB", OK, UINT64_C(1000000000000000000),
		  NULL },
		{ &arg_t_bytesize, "1KiB", OK, 1024, NULL },
		{ &arg_t_bytesize, "1ki", OK, 1024, NULL },
		{ &arg_t_bytesize, "1KIB", OK, 1024, NULL },
		{ &arg_t_bytesize, "16MiB", OK, UINT64_C(16) << 20, NULL },
		{ &arg_t_bytesize, "16Mi", OK, UINT64_C(16) << 20, NULL },
		{ &arg_t_bytesize, "2GiB", OK, UINT64_C(2) << 30, NULL },
		{ &arg_t_bytesize, "2gi", OK, UINT64_C(2) << 30, NULL },
		{ &arg_t_bytesize, "3TiB", OK, UINT64_C(3) << 40, NULL },
		{ &arg_t_bytesize, "3ti", OK, UINT64_C(3) << 40, NULL },
		{ &arg_t_bytesize, "5PiB", OK, UINT64_C(5) << 50, NULL },
		{ &arg_t_bytesize, "5pi", OK, UINT64_C(5) << 50, NULL },
		{ &arg_t_bytesize, "15EiB", OK, UINT64_C(15) << 60, NULL },
		{ &arg_t_bytesize, "15ei", OK, UINT64_C(15) << 60, NULL },
		{ &arg_t_bytesize, "18446744073709551615", OK, UINT64_MAX,
		  NULL },
		{ &arg_t_bytesize, "18446744073709551615b", OK, UINT64_MAX,
		  NULL },
		/* overflow */
		{ &arg_t_bytesize, "18446744073709551616", RNG, 0,
		  "out of range" },
		{ &arg_t_bytesize, "16EiB", RNG, 0, "out of range" },
		{ &arg_t_bytesize, "19e", RNG, 0, "out of range" },
		{ &arg_t_bytesize, "18446744073709552k", RNG, 0,
		  "out of range" },
		{ &arg_t_bytesize, "99999999999999999999999k", RNG, 0,
		  "out of range" },
		/* syntax */
		{ &arg_t_bytesize, "", BAD, 0, NULL },
		{ &arg_t_bytesize, "k", BAD, 0, NULL },
		{ &arg_t_bytesize, "KiB", BAD, 0, NULL },
		{ &arg_t_bytesize, "-1", BAD, 0, NULL },
		{ &arg_t_bytesize, "+1", BAD, 0, NULL },
		{ &arg_t_bytesize, "1.5k", BAD, 0, NULL },
		{ &arg_t_bytesize, "1 k", BAD, 0, NULL },
		{ &arg_t_bytesize, "1x", BAD, 0, NULL },
		{ &arg_t_bytesize, "1kibb", BAD, 0, NULL },
		{ &arg_t_bytesize, "1ib", BAD, 0, NULL },
		{ &arg_t_bytesize, "1bb", BAD, 0, NULL },
		{ &arg_t_bytesize, "0x10", BAD, 0, NULL },
		{ &arg_t_bytesize, "_1k", BAD, 0, NULL },
		{ &arg_t_bytesize, "1_k", BAD, 0, NULL },
		{ &arg_t_bytesize, "1__0", BAD, 0, NULL },
		{ &arg_t_bytesize, "1k1", BAD, 0, NULL },
	};
	size_t i;

	for (i = 0; i < ARG__COUNTOF(tab); i++) {
		struct res r = run(tab[i].t, tab[i].in);
		uint64_t v;

		if (r.st != tab[i].st) {
			TEST_FAIL_("\"%s\": status %d, want %d", tab[i].in,
				   (int)r.st, (int)tab[i].st);
			continue;
		}
		t_->checks++;
		if (r.st != ARG_OK) {
			CHECK(r.untouched);
			if (tab[i].detail)
				CHECK_STR(r.detail, tab[i].detail);
			else
				CHECK(r.detail != NULL);
			continue;
		}
		memcpy(&v, r.dst, sizeof(v));
		if (v != tab[i].want)
			TEST_FAIL_("\"%s\": %llu, want %llu", tab[i].in,
				   (unsigned long long)v,
				   (unsigned long long)tab[i].want);
	}
}

TEST(bytesize_format)
{
	static const struct {
		uint64_t v;
		const char *want;
	} tab[] = {
		{ 0, "0" },
		{ 1, "1" },
		{ 1000, "1000" },
		{ 1024, "1KiB" },
		{ 1536, "1536" },
		{ 3072, "3KiB" },
		{ UINT64_C(16) << 20, "16MiB" },
		{ UINT64_C(1025) << 20, "1025MiB" },
		{ UINT64_C(1) << 30, "1GiB" },
		{ UINT64_C(7) << 40, "7TiB" },
		{ UINT64_C(9) << 50, "9PiB" },
		{ UINT64_C(15) << 60, "15EiB" },
		{ UINT64_MAX, "18446744073709551615" },
	};
	char buf[32];
	size_t i;

	for (i = 0; i < ARG__COUNTOF(tab); i++) {
		uint64_t back = 0;
		int len = arg_t_bytesize.format(&arg_t_bytesize, &tab[i].v,
						buf, sizeof(buf));

		CHECK_STR(buf, tab[i].want);
		CHECK_INT(len, strlen(tab[i].want));
		CHECK_INT(arg_type_parse(&arg_t_bytesize, buf, &back, NULL),
			  ARG_OK);
		CHECK_UINT(back, tab[i].v);
	}
	CHECK_INT(arg_t_bytesize.format(&arg_t_bytesize, &tab[6].v, buf, 3),
		  5);
	CHECK_STR(buf, "16");
}

/* ------------------------------------------------------------------ */
/* duration                                                           */
/* ------------------------------------------------------------------ */

TEST(durations)
{
	static const char *const order = "units must be in order d, h, m, "
					 "s, ms, each once";
	static const struct uint_case tab[] = {
		{ &arg_t_duration, "0", OK, 0, NULL },
		{ &arg_t_duration, "30", OK, 30000, NULL },
		{ &arg_t_duration, "250ms", OK, 250, NULL },
		{ &arg_t_duration, "2s", OK, 2000, NULL },
		{ &arg_t_duration, "1m", OK, 60000, NULL },
		{ &arg_t_duration, "1h", OK, 3600000, NULL },
		{ &arg_t_duration, "1d", OK, 86400000, NULL },
		{ &arg_t_duration, "1m30s", OK, 90000, NULL },
		{ &arg_t_duration, "1h30m", OK, 5400000, NULL },
		{ &arg_t_duration, "1d2h3m4s5ms", OK,
		  UINT64_C(86400000) + 7200000 + 180000 + 4000 + 5, NULL },
		{ &arg_t_duration, "1h5ms", OK, 3600005, NULL },
		{ &arg_t_duration, "90m", OK, 5400000, NULL },
		{ &arg_t_duration, "0s", OK, 0, NULL },
		{ &arg_t_duration, "007s", OK, 7000, NULL },
		{ &arg_t_duration, "18446744073709551615ms", OK, UINT64_MAX,
		  NULL },
		{ &arg_t_duration, "18446744073709551615", RNG, 0,
		  "out of range" },
		{ &arg_t_duration, "18446744073709551616ms", RNG, 0,
		  "out of range" },
		{ &arg_t_duration, "213503982335d", RNG, 0, "out of range" },
		{ &arg_t_duration, "213503982334d1d", BAD, 0, order },
		{ &arg_t_duration, "213503982334d23h59m59s999ms", RNG, 0,
		  "out of range" },
		{ &arg_t_duration, "99999999999999999999999h1m", RNG, 0,
		  "out of range" },
		{ &arg_t_duration, "", BAD, 0, NULL },
		{ &arg_t_duration, "s", BAD, 0, NULL },
		{ &arg_t_duration, "ms", BAD, 0, NULL },
		{ &arg_t_duration, "1x", BAD, 0, NULL },
		{ &arg_t_duration, "1S", BAD, 0, NULL },
		{ &arg_t_duration, "1H", BAD, 0, NULL },
		{ &arg_t_duration, "1MS", BAD, 0, NULL },
		{ &arg_t_duration, "1sec", BAD, 0, NULL },
		{ &arg_t_duration, "1.5s", BAD, 0, NULL },
		{ &arg_t_duration, "-1s", BAD, 0, NULL },
		{ &arg_t_duration, "+1s", BAD, 0, NULL },
		{ &arg_t_duration, "1 s", BAD, 0, NULL },
		{ &arg_t_duration, "1_000ms", BAD, 0, NULL },
		{ &arg_t_duration, "1m30", BAD, 0, NULL },
		{ &arg_t_duration, "1h30", BAD, 0, NULL },
		{ &arg_t_duration, "1s1m", BAD, 0, order },
		{ &arg_t_duration, "30s1h", BAD, 0, order },
		{ &arg_t_duration, "1ms1s", BAD, 0, order },
		{ &arg_t_duration, "1s1s", BAD, 0, order },
		{ &arg_t_duration, "1m1m", BAD, 0, order },
		{ &arg_t_duration, "1h2h", BAD, 0, order },
	};
	size_t i;

	for (i = 0; i < ARG__COUNTOF(tab); i++) {
		struct res r = run(tab[i].t, tab[i].in);
		uint64_t v;

		if (r.st != tab[i].st) {
			TEST_FAIL_("\"%s\": status %d, want %d", tab[i].in,
				   (int)r.st, (int)tab[i].st);
			continue;
		}
		t_->checks++;
		if (r.st != ARG_OK) {
			CHECK(r.untouched);
			if (tab[i].detail)
				CHECK_STR(r.detail, tab[i].detail);
			else
				CHECK(r.detail != NULL);
			continue;
		}
		memcpy(&v, r.dst, sizeof(v));
		if (v != tab[i].want)
			TEST_FAIL_("\"%s\": %llu, want %llu", tab[i].in,
				   (unsigned long long)v,
				   (unsigned long long)tab[i].want);
	}
}

TEST(duration_format)
{
	static const struct {
		uint64_t v;
		const char *want;
	} tab[] = {
		{ 0, "0s" },
		{ 1, "1ms" },
		{ 250, "250ms" },
		{ 1000, "1s" },
		{ 1500, "1s500ms" },
		{ 90000, "1m30s" },
		{ 5400000, "1h30m" },
		{ 86400000, "1d" },
		{ UINT64_C(86400000) + 1, "1d1ms" },
		{ UINT64_MAX, "213503982334d14h25m51s615ms" },
	};
	char buf[40];
	size_t i;

	for (i = 0; i < ARG__COUNTOF(tab); i++) {
		uint64_t back = 1;
		int len = arg_t_duration.format(&arg_t_duration, &tab[i].v,
						buf, sizeof(buf));

		CHECK_STR(buf, tab[i].want);
		CHECK_INT(len, strlen(tab[i].want));
		CHECK_INT(arg_type_parse(&arg_t_duration, buf, &back, NULL),
			  ARG_OK);
		CHECK_UINT(back, tab[i].v);
	}
	CHECK_INT(arg_t_duration.format(&arg_t_duration, &tab[5].v, buf, 3),
		  5);
	CHECK_STR(buf, "1m");
}

TEST(size_duration_round_trip)
{
	uint64_t x = UINT64_C(0xD1B54A32D192ED03);
	size_t k;

	for (k = 0; k < 2000; k++) {
		uint64_t a, b;
		char buf[48];

		x ^= x << 13;
		x ^= x >> 7;
		x ^= x << 17;
		/* vary magnitude and trailing zero bits */
		a = (x >> (k % 64)) << ((k / 7) % 40);
		arg_t_bytesize.format(&arg_t_bytesize, &a, buf, sizeof(buf));
		b = ~a;
		if (arg_type_parse(&arg_t_bytesize, buf, &b, NULL) != ARG_OK ||
		    a != b)
			TEST_FAIL_("size round trip \"%s\"", buf);
		arg_t_duration.format(&arg_t_duration, &a, buf, sizeof(buf));
		b = ~a;
		if (arg_type_parse(&arg_t_duration, buf, &b, NULL) != ARG_OK ||
		    a != b)
			TEST_FAIL_("duration round trip \"%s\"", buf);
		t_->checks += 2;
	}
}

/* ------------------------------------------------------------------ */
/* Decorators                                                         */
/* ------------------------------------------------------------------ */

TEST(range_int)
{
	const arg_type *t = ARG_RANGE_INT(1, 256);
	const char *detail;
	int64_t v = 0;
	char buf[16];

	CHECK_INT(arg_type_parse(t, "1", &v, NULL), ARG_OK);
	CHECK_INT(v, 1);
	CHECK_INT(arg_type_parse(t, "256", &v, NULL), ARG_OK);
	CHECK_INT(v, 256);
	CHECK_INT(arg_type_parse(t, "0x10", &v, NULL), ARG_OK);
	CHECK_INT(v, 16);
	v = 99;
	CHECK_INT(arg_type_parse(t, "0", &v, &detail), ARG_E_RANGE);
	CHECK_STR(detail, "out of range");
	CHECK_INT(arg_type_parse(t, "257", &v, &detail), ARG_E_RANGE);
	CHECK_INT(arg_type_parse(t, "-5", &v, &detail), ARG_E_RANGE);
	CHECK_INT(arg_type_parse(t, "abc", &v, &detail), ARG_E_BAD_VALUE);
	CHECK_STR(detail, "expected an integer");
	CHECK_INT(arg_type_parse(t, "99999999999999999999", &v, &detail),
		  ARG_E_RANGE);
	CHECK_INT(v, 99);
	v = 42;
	CHECK_INT(t->format(t, &v, buf, sizeof(buf)), 2);
	CHECK_STR(buf, "42");
	CHECK_STR(t->name, "N");
	CHECK_INT(t->kind, ARG_K_INT);

	t = ARG_RANGE_INT(INT64_MIN, -1);
	CHECK_INT(arg_type_parse(t, "-9223372036854775808", &v, NULL),
		  ARG_OK);
	CHECK_INT(v, INT64_MIN);
	CHECK_INT(arg_type_parse(t, "0", &v, NULL), ARG_E_RANGE);
}

TEST(range_uint_and_double)
{
	const arg_type *u = ARG_RANGE_UINT(10, UINT64_MAX);
	uint64_t v = 0;
	char buf[32];

	CHECK_INT(arg_type_parse(u, "10", &v, NULL), ARG_OK);
	CHECK_UINT(v, 10);
	CHECK_INT(arg_type_parse(u, "18446744073709551615", &v, NULL),
		  ARG_OK);
	CHECK_UINT(v, UINT64_MAX);
	CHECK_INT(arg_type_parse(u, "9", &v, NULL), ARG_E_RANGE);
	CHECK_INT(arg_type_parse(u, "-1", &v, NULL), ARG_E_BAD_VALUE);
	CHECK_INT(u->format(u, &v, buf, sizeof(buf)), 20);

#ifndef ARG_NO_FLOAT
	{
		const arg_type *d = ARG_RANGE_DOUBLE(0.0, 1.0);
		double x = 5;

		CHECK_INT(arg_type_parse(d, "0", &x, NULL), ARG_OK);
		CHECK(x == 0.0);
		CHECK_INT(arg_type_parse(d, "1", &x, NULL), ARG_OK);
		CHECK(x == 1.0);
		CHECK_INT(arg_type_parse(d, ".5", &x, NULL), ARG_OK);
		CHECK(x == 0.5);
		CHECK_INT(arg_type_parse(d, "1.0000001", &x, NULL),
			  ARG_E_RANGE);
		CHECK_INT(arg_type_parse(d, "-0.1", &x, NULL), ARG_E_RANGE);
		CHECK_INT(arg_type_parse(d, "nan", &x, NULL), ARG_E_RANGE);
		CHECK_INT(arg_type_parse(d, "inf", &x, NULL), ARG_E_RANGE);
		CHECK(x == 0.5);
		CHECK_INT(d->format(d, &x, buf, sizeof(buf)), 3);
		CHECK_STR(buf, "0.5");
	}
#endif
}

/* Range decorators over other inner types, built by hand. */
TEST(range_custom_inner)
{
	static const arg_range r8 = { .inner = &arg_t_i8,
				      .lo.i = -10, .hi.i = 10 };
	static const arg_type t8 = { .name = "N", .size = 1,
				     .parse = arg_range_parse,
				     .format = arg_wrap_format, .ud = &r8,
				     .kind = ARG_K_INT };
	static const arg_range rsz = { .inner = &arg_t_bytesize,
				       .lo.u = 1024, .hi.u = 1 << 20 };
	static const arg_type tsz = { .name = "SIZE", .size = 8,
				      .parse = arg_range_parse,
				      .format = arg_wrap_format, .ud = &rsz,
				      .kind = ARG_K_UINT };
	static const arg_range rbad = { .inner = &arg_t_string };
	static const arg_type tbad = { .name = "X", .size = 8,
				       .parse = arg_range_parse, .ud = &rbad,
				       .kind = ARG_K_STR };
	static const arg_type tmis = { .name = "X", .size = 8,
				       .parse = arg_range_parse, .ud = &rbad,
				       .kind = ARG_K_INT };
	static const arg_type tnoud = { .name = "X", .size = 8,
					.parse = arg_range_parse,
					.kind = ARG_K_INT };
	static const arg_type big = { .name = "B", .size = 32,
				      .parse = arg_range_parse,
				      .kind = ARG_K_INT };
	static const arg_range rbig = { .inner = &big };
	static const arg_type tbig = { .name = "X", .size = 32,
				       .parse = arg_range_parse, .ud = &rbig,
				       .kind = ARG_K_INT };
	const char *detail;
	unsigned char raw[16];
	int8_t i8;
	uint64_t u;
	struct res r;

	memset(raw, 0xA5, sizeof(raw));
	CHECK_INT(arg_type_parse(&t8, "-10", raw, NULL), ARG_OK);
	memcpy(&i8, raw, 1);
	CHECK_INT(i8, -10);
	CHECK_INT(raw[1], 0xA5); /* exactly inner->size bytes */
	CHECK_INT(arg_type_parse(&t8, "11", raw, NULL), ARG_E_RANGE);
	CHECK_INT(arg_type_parse(&t8, "200", raw, &detail), ARG_E_RANGE);

	CHECK_INT(arg_type_parse(&tsz, "1KiB", &u, NULL), ARG_OK);
	CHECK_UINT(u, 1024);
	CHECK_INT(arg_type_parse(&tsz, "1k", &u, NULL), ARG_E_RANGE);
	CHECK_INT(arg_type_parse(&tsz, "2MiB", &u, NULL), ARG_E_RANGE);

#ifndef ARG_NO_FLOAT
	{
		static const arg_range rf = { .inner = &arg_t_float,
					      .lo.d = -1.0, .hi.d = 1.0 };
		static const arg_type tf = { .name = "NUM", .size = 4,
					     .parse = arg_range_parse,
					     .ud = &rf, .kind = ARG_K_DOUBLE };
		float f;

		CHECK_INT(arg_type_parse(&tf, "0.5", &f, NULL), ARG_OK);
		CHECK(f == 0.5f);
		CHECK_INT(arg_type_parse(&tf, "2", &f, NULL), ARG_E_RANGE);
	}
#endif

	/* non-numeric decorator kind */
	CHECK_INT(arg_type_parse(&tbad, "x", raw, &detail), ARG_E_BAD_VALUE);
	CHECK_STR(detail, "invalid type");
	/* kind INT over a string inner type */
	CHECK_INT(arg_type_parse(&tmis, "x", raw, &detail), ARG_E_BAD_VALUE);
	CHECK_STR(detail, "invalid type");
	CHECK_INT(arg_type_parse(&tnoud, "1", raw, NULL), ARG_E_BAD_VALUE);
	CHECK_INT(arg_type_parse(&tbig, "1", raw, NULL), ARG_E_BAD_VALUE);

	r = run(&t8, "1");
	CHECK_INT(r.st, ARG_OK);
	r = run(&t8, "99");
	CHECK(r.untouched);
}

TEST(nonempty_and_maxlen)
{
	const arg_type *ne = ARG_NONEMPTY(&arg_t_string);
	const arg_type *ml = ARG_MAXLEN(&arg_t_string, 3);
	const char *v = NULL, *in = "abc", *detail;
	char buf[8];

	CHECK_INT(arg_type_parse(ne, in, &v, NULL), ARG_OK);
	CHECK(v == in);
	v = NULL;
	CHECK_INT(arg_type_parse(ne, "", &v, &detail), ARG_E_BAD_VALUE);
	CHECK_STR(detail, "must not be empty");
	CHECK(v == NULL);

	CHECK_INT(arg_type_parse(ml, "", &v, NULL), ARG_OK);
	CHECK_INT(arg_type_parse(ml, "abc", &v, NULL), ARG_OK);
	v = NULL;
	CHECK_INT(arg_type_parse(ml, "abcd", &v, &detail), ARG_E_BAD_VALUE);
	CHECK_STR(detail, "too long");
	CHECK(v == NULL);

	v = "hey";
	CHECK_INT(ne->format(ne, &v, buf, sizeof(buf)), 3);
	CHECK_STR(buf, "hey");
	CHECK_INT(ml->format(ml, &v, buf, sizeof(buf)), 3);
	CHECK_STR(ne->name, "TEXT");
	CHECK_INT(ne->kind, ARG_K_STR);
}

TEST(one_of)
{
	const arg_type *t = ARG_ONE_OF_STR("auto", "always", "never");
	static const arg_type noud = { .name = "WORD",
				       .size = sizeof(const char *),
				       .parse = arg_one_of_parse,
				       .kind = ARG_K_STR };
	const char *v = NULL, *in = "never", *detail;
	arg_ctx ctx = { NULL, NULL, NULL, ARG_E_BAD_VALUE };

	CHECK_INT(arg_type_parse(t, in, &v, NULL), ARG_OK);
	CHECK(v == in);
	CHECK_INT(arg_type_parse(t, "auto", &v, NULL), ARG_OK);
	CHECK_STR(v, "auto");
	v = NULL;
	CHECK_INT(arg_type_parse(t, "Auto", &v, &detail), ARG_E_BAD_VALUE);
	CHECK_STR(detail, "invalid choice");
	CHECK_INT(arg_type_parse(t, "", &v, NULL), ARG_E_BAD_VALUE);
	CHECK_INT(arg_type_parse(t, "alway", &v, NULL), ARG_E_BAD_VALUE);
	CHECK_INT(arg_type_parse(t, "alwayss", &v, NULL), ARG_E_BAD_VALUE);
	CHECK(v == NULL);
	/* only n bytes count */
	CHECK_INT(t->parse(t, "autox", 4, &v, &ctx), 0);
	CHECK_INT(arg_type_parse(&noud, "auto", &v, &detail),
		  ARG_E_BAD_VALUE);
	CHECK_STR(detail, "invalid type");
	CHECK(t->format == NULL);
	CHECK_STR(t->name, "WORD");
}

TEST(decorators_compose)
{
	const arg_type *t = ARG_NONEMPTY(ARG_MAXLEN(
		ARG_ONE_OF_STR("red", "green", "blue"), 4));
	const arg_type *bad = ARG_NONEMPTY(ARG_NONEMPTY(NULL));
	const char *v = NULL, *detail;
	char buf[8];

	CHECK_INT(arg_type_parse(t, "red", &v, NULL), ARG_OK);
	CHECK_STR(v, "red");
	CHECK_INT(arg_type_parse(t, "blue", &v, NULL), ARG_OK);
	CHECK_INT(arg_type_parse(t, "", &v, &detail), ARG_E_BAD_VALUE);
	CHECK_STR(detail, "must not be empty");
	CHECK_INT(arg_type_parse(t, "green", &v, &detail), ARG_E_BAD_VALUE);
	CHECK_STR(detail, "too long");
	CHECK_INT(arg_type_parse(t, "pink", &v, &detail), ARG_E_BAD_VALUE);
	CHECK_STR(detail, "invalid choice");
	/* one-of has no format, so the chain has none */
	CHECK_INT(t->format(t, &v, buf, sizeof(buf)), -1);
	CHECK_INT(arg_type_parse(bad, "x", &v, &detail), ARG_E_BAD_VALUE);
	CHECK_STR(detail, "invalid type");

	/* a range over a range */
	{
		const arg_range outer = {
			.inner = &(const arg_type){
				.name = "N", .size = 8,
				.parse = arg_range_parse,
				.format = arg_wrap_format,
				.ud = &(const arg_range){ .inner = &arg_t_i64,
							  .lo.i = 0,
							  .hi.i = 100 },
				.kind = ARG_K_INT },
			.lo.i = 50,
			.hi.i = 1000,
		};
		const arg_type rr = { .name = "N", .size = 8,
					     .parse = arg_range_parse,
					     .format = arg_wrap_format,
					     .ud = &outer,
					     .kind = ARG_K_INT };
		int64_t x;

		CHECK_INT(arg_type_parse(&rr, "75", &x, NULL), ARG_OK);
		CHECK_INT(x, 75);
		CHECK_INT(arg_type_parse(&rr, "25", &x, NULL), ARG_E_RANGE);
		CHECK_INT(arg_type_parse(&rr, "500", &x, NULL), ARG_E_RANGE);
		CHECK_INT(rr.format(&rr, &x, buf, sizeof(buf)), 2);
		CHECK_STR(buf, "75");
	}
}

TEST(wrap_format_errors)
{
	static const arg_type noud = { .name = "X", .size = 8,
				       .format = arg_wrap_format };
	static const arg_wrap nul = { .inner = NULL };
	static const arg_type noinner = { .name = "X", .size = 8,
					  .format = arg_wrap_format,
					  .ud = &nul };
	static const arg_wrap e = { .inner = &arg_t_enum };
	static const arg_type wenum = { .name = "X", .size = 8,
					.format = arg_wrap_format,
					.ud = &e };
	int v = 0;
	char buf[8];

	CHECK_INT(arg_wrap_format(NULL, &v, buf, sizeof(buf)), -1);
	CHECK_INT(arg_wrap_format(&noud, &v, buf, sizeof(buf)), -1);
	CHECK_INT(arg_wrap_format(&noinner, &v, buf, sizeof(buf)), -1);
	CHECK_INT(arg_wrap_format(&wenum, &v, buf, sizeof(buf)), -1);
}

TEST(maxlen_bad_inner)
{
	static const arg_wrap w = { .inner = NULL, .n = 5 };
	static const arg_type t = { .name = "X", .size = 8,
				    .parse = arg_maxlen_parse, .ud = &w,
				    .kind = ARG_K_STR };
	static const arg_type t2 = { .name = "X", .size = 8,
				     .parse = arg_maxlen_parse,
				     .kind = ARG_K_STR };
	static const arg_type t3 = { .name = "X", .size = 8,
				     .parse = arg_nonempty_parse,
				     .kind = ARG_K_STR };
	const char *v;

	CHECK_INT(arg_type_parse(&t, "x", &v, NULL), ARG_E_BAD_VALUE);
	CHECK_INT(arg_type_parse(&t2, "x", &v, NULL), ARG_E_BAD_VALUE);
	CHECK_INT(arg_type_parse(&t3, "x", &v, NULL), ARG_E_BAD_VALUE);
}

/* ------------------------------------------------------------------ */
/* Loads and comparisons                                              */
/* ------------------------------------------------------------------ */

TEST(load_helpers)
{
	static const arg_type odd_i = { .size = 3, .kind = ARG_K_INT };
	static const arg_type odd_u = { .size = 3, .kind = ARG_K_UINT };
	static const arg_type odd_d = { .size = 2, .kind = ARG_K_DOUBLE };
	int8_t i8 = -5;
	int16_t i16 = -300;
	int32_t i32 = -70000;
	int64_t i64 = INT64_MIN, si;
	uint8_t u8 = 250;
	uint16_t u16 = 65000;
	uint32_t u32 = 4000000000u;
	uint64_t u64 = UINT64_MAX, su;
	int e = 3;
	bool b = true;
	double d = 2.5, sd;
	float f = -1.25f;
	const char *s = "x";

	CHECK(arg__load_int(&arg_t_i8, &i8, &si) && si == -5);
	CHECK(arg__load_int(&arg_t_i16, &i16, &si) && si == -300);
	CHECK(arg__load_int(&arg_t_i32, &i32, &si) && si == -70000);
	CHECK(arg__load_int(&arg_t_i64, &i64, &si) && si == INT64_MIN);
	CHECK(arg__load_int(&arg_t_enum, &e, &si) && si == 3);
	CHECK(arg__load_int(&arg_t_bool, &b, &si) && si == 1);
	b = false;
	CHECK(arg__load_int(&arg_t_bool, &b, &si) && si == 0);
	CHECK(!arg__load_int(&arg_t_u8, &u8, &si));
	CHECK(!arg__load_int(&arg_t_string, &s, &si));
	CHECK(!arg__load_int(&arg_t_double, &d, &si));
	CHECK(!arg__load_int(&odd_i, &i32, &si));
	CHECK(!arg__load_int(NULL, &i32, &si));
	CHECK(!arg__load_int(&arg_t_i32, NULL, &si));

	CHECK(arg__load_uint(&arg_t_u8, &u8, &su) && su == 250);
	CHECK(arg__load_uint(&arg_t_u16, &u16, &su) && su == 65000);
	CHECK(arg__load_uint(&arg_t_u32, &u32, &su) && su == 4000000000u);
	CHECK(arg__load_uint(&arg_t_u64, &u64, &su) && su == UINT64_MAX);
	CHECK(arg__load_uint(&arg_t_duration, &u64, &su));
	CHECK(!arg__load_uint(&arg_t_i64, &i64, &su));
	CHECK(!arg__load_uint(&odd_u, &u32, &su));
	CHECK(!arg__load_uint(NULL, &u32, &su));
	CHECK(!arg__load_uint(&arg_t_u8, NULL, &su));

	CHECK(arg__load_double(&arg_t_double, &d, &sd) && sd == 2.5);
	CHECK(arg__load_double(&arg_t_float, &f, &sd) && sd == -1.25);
	CHECK(!arg__load_double(&odd_d, &d, &sd));
	CHECK(!arg__load_double(&arg_t_i64, &i64, &sd));
	CHECK(!arg__load_double(NULL, &d, &sd));
	CHECK(!arg__load_double(&arg_t_double, NULL, &sd));
}

TEST(load_misaligned)
{
	unsigned char raw[16];
	int64_t v = -123456789, out;
	double d = 0.75, dout;

	memcpy(raw + 1, &v, sizeof(v));
	CHECK(arg__load_int(&arg_t_i64, raw + 1, &out));
	CHECK_INT(out, -123456789);
	memcpy(raw + 3, &d, sizeof(d));
	CHECK(arg__load_double(&arg_t_double, raw + 3, &dout));
	CHECK(dout == 0.75);
}

TEST(compare_helper)
{
	static const arg_type odd = { .size = 3, .kind = ARG_K_INT };
	int64_t a = -5, b = 7;
	uint64_t ua = 5, ub = UINT64_MAX;
	uint8_t s1 = 200, s2 = 100;
	double da = 1.5, db = -2.0, dn;
	float fa = 1.0f, fb = 2.0f;
	int ea = 1, eb = 2;
	bool ba = true, bb = false;
	const char *sa = "a", *sb = "b";
	int cmp = 99;

	CHECK(arg__compare(&arg_t_i64, &a, &b, &cmp) && cmp < 0);
	CHECK(arg__compare(&arg_t_i64, &b, &a, &cmp) && cmp > 0);
	CHECK(arg__compare(&arg_t_i64, &a, &a, &cmp) && cmp == 0);
	CHECK(arg__compare(&arg_t_u64, &ua, &ub, &cmp) && cmp < 0);
	CHECK(arg__compare(&arg_t_u64, &ub, &ua, &cmp) && cmp > 0);
	CHECK(arg__compare(&arg_t_u64, &ub, &ub, &cmp) && cmp == 0);
	CHECK(arg__compare(&arg_t_u8, &s1, &s2, &cmp) && cmp > 0);
	CHECK(arg__compare(&arg_t_double, &da, &db, &cmp) && cmp > 0);
	CHECK(arg__compare(&arg_t_double, &db, &da, &cmp) && cmp < 0);
	CHECK(arg__compare(&arg_t_double, &da, &da, &cmp) && cmp == 0);
	CHECK(arg__compare(&arg_t_float, &fa, &fb, &cmp) && cmp < 0);
	CHECK(arg__compare(&arg_t_bytesize, &ua, &ub, &cmp) && cmp < 0);

#ifndef ARG_NO_FLOAT
	CHECK_INT(arg_type_parse(&arg_t_double, "nan", &dn, NULL), ARG_OK);
#else
	{
		uint64_t bits = UINT64_C(0x7ff8000000000000);

		memcpy(&dn, &bits, sizeof(dn));
	}
#endif
	CHECK(!arg__compare(&arg_t_double, &dn, &da, &cmp));
	CHECK(!arg__compare(&arg_t_double, &da, &dn, &cmp));

	CHECK(!arg__compare(&arg_t_enum, &ea, &eb, &cmp));
	CHECK(!arg__compare(&arg_t_bool, &ba, &bb, &cmp));
	CHECK(!arg__compare(&arg_t_string, &sa, &sb, &cmp));
	CHECK(!arg__compare(NULL, &a, &b, &cmp));
	CHECK(!arg__compare(&odd, &a, &b, &cmp));
	{
		static const arg_type oddu = { .size = 3,
					       .kind = ARG_K_UINT };
		static const arg_type oddd = { .size = 3,
					       .kind = ARG_K_DOUBLE };

		CHECK(!arg__compare(&oddu, &ua, &ub, &cmp));
		CHECK(!arg__compare(&oddd, &da, &db, &cmp));
	}
}

#ifndef ARG_NO_FLOAT
#define FLOAT_TESTS                                                          \
	TEST_CASE(doubles), TEST_CASE(doubles_special),                      \
	TEST_CASE(doubles_long_input), TEST_CASE(doubles_short_input_window),\
	TEST_CASE(floats), TEST_CASE(double_format),                         \
	TEST_CASE(double_round_trip), TEST_CASE(double_locale_independent)
#else
#define FLOAT_TESTS TEST_CASE(float_disabled)
#endif

TEST_MAIN(TEST_CASE(hexval), TEST_CASE(ieq), TEST_CASE(reject_sets_context),
	  TEST_CASE(type_parse_entry), TEST_CASE(type_metadata),
	  TEST_CASE(bool_values), TEST_CASE(bool_format),
	  TEST_CASE(signed_integers), TEST_CASE(unsigned_integers),
	  TEST_CASE(integer_unsupported_size),
	  TEST_CASE(integer_short_input_window), TEST_CASE(integer_format),
	  TEST_CASE(integer_round_trip),
	  FLOAT_TESTS,
	  TEST_CASE(strings), TEST_CASE(enums), TEST_CASE(bytesizes),
	  TEST_CASE(bytesize_format), TEST_CASE(durations),
	  TEST_CASE(duration_format), TEST_CASE(size_duration_round_trip),
	  TEST_CASE(range_int), TEST_CASE(range_uint_and_double),
	  TEST_CASE(range_custom_inner), TEST_CASE(nonempty_and_maxlen),
	  TEST_CASE(one_of), TEST_CASE(decorators_compose),
	  TEST_CASE(wrap_format_errors), TEST_CASE(maxlen_bad_inner),
	  TEST_CASE(load_helpers), TEST_CASE(load_misaligned),
	  TEST_CASE(compare_helper))
