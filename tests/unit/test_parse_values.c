// SPDX-License-Identifier: GPL-2.0-only
/*
 * test_parse_values.c - typed values, lists, positionals, ranges,
 * .check functions and .to bindings (SDD 6.3, 6.4, 8, 10)
 */
#include "parse_util.h"

#include <float.h>
#include <limits.h>

TEST(lists)
{
	const arg_item spec[] = { ARG_APP(
		"t", "T", ARG_STR('I', "include", "Paths", .many = 1),
		ARG_STR('t', "tags", "Tags", .many = 1, .sep = ','),
		ARG_INT('n', "num", "Numbers", .many = 1, .sep = ':')) };
	struct fx f;
	size_t i;

	fx_init(&f, spec);
	REQUIRE(PARSE(&f, "-I", "a", "--include=b", "-Ic") == ARG_OK);
	CHECK_UINT(arg_count(&f.p, "include"), 3);
	CHECK_STR(arg_str(&f.p, "include"), "a");
	CHECK_STR(arg_str_at(&f.p, "include", 2), "c");
	CHECK(arg_str_at(&f.p, "include", 3) == NULL);
	CHECK_UINT(arg_count(&f.p, "tags"), 0);
	CHECK(arg_str(&f.p, "tags") == NULL);

	REQUIRE(PARSE(&f, "--tags=a,b", "-t", ",c,", "-t", "") == ARG_OK);
	CHECK_UINT(arg_count(&f.p, "tags"), 6);
	CHECK_STR(arg_str_at(&f.p, "tags", 0), "a");
	CHECK_STR(arg_str_at(&f.p, "tags", 1), "b");
	CHECK_STR(arg_str_at(&f.p, "tags", 2), "");
	CHECK_STR(arg_str_at(&f.p, "tags", 3), "c");
	CHECK_STR(arg_str_at(&f.p, "tags", 4), "");
	CHECK_STR(arg_str_at(&f.p, "tags", 5), "");

	/* many values: the chunks grow, every element stays reachable */
	REQUIRE(PARSE(&f, "-n", "1:2:3:4:5:6:7:8:9:10:11:12:13:14:15:16:17")
		== ARG_OK);
	CHECK_UINT(arg_count(&f.p, "num"), 17);
	for (i = 0; i < 17; i++)
		CHECK_INT(arg_int_at(&f.p, "num", i), (int64_t)i + 1);
	CHECK_INT(arg_int_at(&f.p, "num", 17), 0);

	CHECK_INT(PARSE(&f, "-n", "1:x:3"), ARG_E_BAD_VALUE);
	CHECK_STRN(fx_error(&f)->text, fx_error(&f)->text_len, "x");
	CHECK_INT(fx_error(&f)->argi, 2);
}

TEST(positionals)
{
	const arg_item spec[] = { ARG_APP(
		"t", "T", ARG_ARG("src", "Source"),
		ARG_ARG("n", "Count", .type = &arg_t_u8),
		ARG_ARG_OPT("mode", "Mode", .def = "fast"),
		ARG_ARGS_OPT("files", "Files", .max_count = 2)) };
	struct fx f;
	const arg_error *e;

	fx_init(&f, spec);
	CHECK_INT(PARSE0(&f), ARG_E_TOO_FEW_ARGS);
	CHECK(fx_error(&f)->item == &spec[0].items[0]);
	CHECK_INT(PARSE(&f, "a"), ARG_E_TOO_FEW_ARGS);
	CHECK(fx_error(&f)->item == &spec[0].items[1]);
	CHECK_STR(fx_report(&f),
		  "prog: missing required argument <n>\n"
		  "Try 'prog --help' for more information.\n");
	REQUIRE(PARSE(&f, "a", "7") == ARG_OK);
	CHECK_STR(arg_str(&f.p, "src"), "a");
	CHECK_UINT(arg_uint(&f.p, "n"), 7);
	CHECK_STR(arg_str(&f.p, "mode"), "fast");
	CHECK_INT(arg_source(&f.p, "mode"), ARG_SRC_DEFAULT);
	CHECK_UINT(arg_count(&f.p, "files"), 0);
	REQUIRE(PARSE(&f, "a", "7", "slow", "x", "y") == ARG_OK);
	CHECK_STR(arg_str(&f.p, "mode"), "slow");
	CHECK_STR(arg_str_at(&f.p, "files", 1), "y");
	CHECK_INT(PARSE(&f, "a", "7", "slow", "x", "y", "z"),
		  ARG_E_TOO_MANY_ARGS);
	CHECK_INT(PARSE(&f, "a", "300"), ARG_E_RANGE);
	e = fx_error(&f);
	CHECK_STR(e->detail, "out of range");
	CHECK_STR(fx_report(&f),
		  "prog: value '300' for '<n>' is out of range: out of "
		  "range\nTry 'prog --help' for more information.\n");
}

TEST(variadic_minimum)
{
	const arg_item spec[] = { ARG_APP(
		"t", "T",
		{ .kind = ARG_I_ARG, .name = "files", .min_count = 2,
		  .many = 1, .type = &arg_t_i32 }) };
	struct fx f;

	fx_init(&f, spec);
	CHECK_INT(PARSE(&f, "1"), ARG_E_TOO_FEW_ARGS);
	REQUIRE(PARSE(&f, "1", "-2", "3") == ARG_OK);
	CHECK_UINT(arg_count(&f.p, "files"), 3);
	CHECK_INT(arg_int_at(&f.p, "files", 1), -2);
}

TEST(typed_values)
{
	const arg_item spec[] = { ARG_APP(
		"t", "T", ARG_DOUBLE('d', "ratio", "Ratio"),
		ARG_UINT('u', "count", "Count"),
		ARG_SIZE('s', "size", "Size"),
		ARG_DURATION(0, "timeout", "Timeout"),
		ARG_ENUM('f', "format", "Format", ARG_CHOICES("png", "jpg")),
		ARG_CUSTOM(0, "small", &arg_t_i8, "Small"),
		ARG_CUSTOM(0, "word", ARG_ONE_OF_STR("a", "b"), "Word"),
		ARG_CUSTOM(0, "pct", ARG_RANGE_INT(0, 100), "Percent")) };
	struct fx f;
	int8_t small = 0;

	fx_init(&f, spec);
	REQUIRE(PARSE(&f, "-d", "0.5", "-u", "9", "-s", "4k", "--timeout",
		      "1m30s", "-fjpg", "--small=-7", "--word", "b",
		      "--pct=42") == ARG_OK);
	CHECK_DBL(arg_double(&f.p, "ratio"), 0.5, 0);
	CHECK_UINT(arg_uint(&f.p, "count"), 9);
	CHECK_UINT(arg_uint(&f.p, "size"), 4000);
	CHECK_UINT(arg_uint(&f.p, "timeout"), 90000);
	CHECK_INT(arg_enum(&f.p, "format"), 1);
	CHECK_INT(arg_int(&f.p, "format"), 1);
	CHECK_STR(arg_str(&f.p, "format"), "jpg");
	CHECK_STR(arg_str_at(&f.p, "format", 1), NULL);
	CHECK_INT(arg_int(&f.p, "small"), -7);
	CHECK(arg_get(&f.p, "small", &small));
	CHECK_INT(small, -7);
	CHECK_STR(arg_str(&f.p, "word"), "b");
	CHECK_INT(arg_int(&f.p, "pct"), 42);
	REQUIRE(PARSE0(&f) == ARG_OK);
	CHECK_INT(arg_enum(&f.p, "format"), -1);
	CHECK_DBL(arg_double(&f.p, "ratio"), 0, 0);
	CHECK(!arg_get(&f.p, "small", &small));
	CHECK_INT(PARSE(&f, "-f", "gif"), ARG_E_BAD_VALUE);
	CHECK_STR(fx_error(&f)->detail, "invalid choice");
	CHECK_INT(PARSE(&f, "--pct=101"), ARG_E_RANGE);
	CHECK_INT(PARSE(&f, "-u", "-1"), ARG_E_BAD_VALUE);
}

/* SDD 8.4: a custom type */
static int parse_rgb(const arg_type *t, const char *s, size_t n, void *dst,
		     arg_ctx *ctx)
{
	uint32_t v = 0;
	size_t i;

	(void)t;
	if (n != 7 || s[0] != '#')
		return arg_reject(ctx, "expected #RRGGBB");
	for (i = 1; i < 7; i++) {
		int d = arg_hexval(s[i]);

		if (d < 0)
			return arg_reject(ctx, "invalid hex digit");
		v = (v << 4) | (uint32_t)d;
	}
	memcpy(dst, &v, sizeof(v));
	return 0;
}

static const arg_type t_rgb = { .name = "COLOR", .size = sizeof(uint32_t),
				.parse = parse_rgb };

TEST(custom_type)
{
	uint32_t c = 0, bound = 0;
	const arg_item spec[] = { ARG_APP(
		"t", "T", ARG_CUSTOM('c', "color", &t_rgb, "Accent",
				     .def = "#ff8800", .to = ARG_TO_RAW(&bound),
				     .many = 1)) };
	struct fx f;

	fx_init(&f, spec);
	REQUIRE(PARSE0(&f) == ARG_OK);
	CHECK(arg_get(&f.p, "color", &c));
	CHECK_UINT(c, 0xff8800);
	CHECK_UINT(bound, 0xff8800);
	REQUIRE(PARSE(&f, "-c", "#000001", "-c#000002") == ARG_OK);
	CHECK(arg_get_at(&f.p, "color", 1, &c));
	CHECK_UINT(c, 2);
	CHECK(!arg_get_at(&f.p, "color", 2, &c));
	CHECK_UINT(bound, 2); /* the last value */
	CHECK_INT(PARSE(&f, "-c", "#00000g"), ARG_E_BAD_VALUE);
	CHECK_STR(fx_report(&f),
		  "prog: invalid value '#00000g' for '--color <COLOR>': "
		  "invalid hex digit\n"
		  "Try 'prog --help' for more information.\n");
}

TEST(min_max)
{
	const arg_item spec[] = { ARG_APP(
		"t", "T", ARG_INT('q', "quality", "Q", .def = "80", .min = "1",
				  .max = "100"),
		ARG_DOUBLE('r', "ratio", "R", .min = "0.5"),
		ARG_UINT('u', "u", "U", .max = "0x10", .many = 1),
		ARG_SIZE('s', "size", "S", .max = "1k"),
		ARG_COUNT('v', "verbose", "V", .max = "2"),
		ARG_ARG_OPT("pos", "P", .type = &arg_t_i8, .min = "-3")) };
	struct fx f;

	fx_init(&f, spec);
	REQUIRE(PARSE0(&f) == ARG_OK);
	CHECK_INT(arg_int(&f.p, "quality"), 80);
	REQUIRE(PARSE(&f, "-q1", "-q100", "-r0.5", "-u16", "-s1000", "-vv",
		      "-3") == ARG_OK);
	CHECK_INT(PARSE(&f, "-q0"), ARG_E_RANGE);
	CHECK_STR(fx_error(&f)->detail, "must be between 1 and 100");
	CHECK_STR(fx_report(&f),
		  "prog: value '0' for '--quality <N>' is out of range: "
		  "must be between 1 and 100\n"
		  "Try 'prog --help' for more information.\n");
	CHECK_INT(PARSE(&f, "--quality=101"), ARG_E_RANGE);
	CHECK_INT(PARSE(&f, "-r", "0.25"), ARG_E_RANGE);
	CHECK_STR(fx_error(&f)->detail, "must be at least 0.5");
	CHECK_STRN(fx_error(&f)->text, fx_error(&f)->text_len, "0.25");
	CHECK_INT(PARSE(&f, "-u1", "-u17"), ARG_E_RANGE);
	CHECK_STR(fx_error(&f)->detail, "must be at most 0x10");
	CHECK_INT(PARSE(&f, "-s1001"), ARG_E_RANGE);
	CHECK_STRN(fx_error(&f)->text, fx_error(&f)->text_len, "1001");
	CHECK_INT(PARSE(&f, "-vvv"), ARG_E_RANGE);
	CHECK_STRN(fx_error(&f)->text, fx_error(&f)->text_len, "3");
	CHECK_INT(PARSE(&f, "--", "-4"), ARG_E_RANGE);
}

static int must_be_even(arg *a, const char *key)
{
	if (arg_int(a, key) % 2)
		return arg_fail(a, "must be even");
	return 0;
}

static int check_key(arg *a, const char *key)
{
	return strcmp(key, "-k") == 0 && arg_flag(a, key) ? 0 : 1;
}

static int no_message(arg *a, const char *key)
{
	(void)a;
	(void)key;
	return 1;
}

TEST(check_functions)
{
	const arg_item spec[] = { ARG_APP(
		"t", "T", ARG_INT('n', "num", "N", .check = must_be_even),
		ARG_FLAG('k', NULL, "K", .check = check_key),
		ARG_ENUM(0, "e", "E", ARG_CHOICES("x", "y"),
			 .check = no_message),
		ARG_STR(0, "s", "S", .check = no_message),
		ARG_COUNT(0, "c", "C", .check = no_message)) };
	struct fx f;

	fx_init(&f, spec);
	REQUIRE(PARSE0(&f) == ARG_OK); /* unset items are not checked */
	REQUIRE(PARSE(&f, "-n4", "-k") == ARG_OK);
	CHECK_INT(PARSE(&f, "-n3"), ARG_E_BAD_VALUE);
	CHECK_STR(fx_error(&f)->detail, "must be even");
	CHECK_STR(fx_report(&f),
		  "prog: invalid value '3' for '--num <N>': must be even\n"
		  "Try 'prog --help' for more information.\n");
	CHECK_INT(PARSE(&f, "--e=y"), ARG_E_BAD_VALUE);
	CHECK(fx_error(&f)->detail == NULL);
	CHECK_STRN(fx_error(&f)->text, fx_error(&f)->text_len, "y");
	CHECK_INT(PARSE(&f, "--s=hi"), ARG_E_BAD_VALUE);
	CHECK_STRN(fx_error(&f)->text, fx_error(&f)->text_len, "hi");
	CHECK_INT(PARSE(&f, "--c"), ARG_E_BAD_VALUE);
	CHECK(fx_error(&f)->text == NULL);
}

TEST(bindings)
{
	bool b = false, nb = true;
	int i = 0, cnt = 0;
	long l = 0;
	int64_t i64 = 0;
	unsigned u = 0;
	unsigned long ul = 0;
	uint64_t u64 = 0;
	size_t sz = 0;
	double d = 0;
	float fl = 0;
	const char *s = NULL;
	const arg_item spec[] = { ARG_APP(
		"t", "T", ARG_FLAG('b', "b", "B", .to = ARG_TO_BOOL(&b)),
		ARG_FLAG(0, "nb", "NB", .to = ARG_TO_BOOL(&nb)),
		ARG_INT(0, "i", "I", .to = ARG_TO_INT(&i)),
		ARG_INT(0, "l", "L", .to = ARG_TO_LONG(&l)),
		ARG_INT(0, "i64", "I", .to = ARG_TO_I64(&i64)),
		ARG_UINT(0, "u", "U", .to = ARG_TO_UINT(&u)),
		ARG_INT(0, "ul", "U", .to = ARG_TO_ULONG(&ul)),
		ARG_UINT(0, "u64", "U", .to = ARG_TO_U64(&u64)),
		ARG_SIZE(0, "sz", "S", .to = ARG_TO_SIZE(&sz)),
		ARG_DOUBLE(0, "d", "D", .to = ARG_TO_DOUBLE(&d)),
		ARG_DOUBLE(0, "fl", "F", .to = ARG_TO_FLOAT(&fl)),
		ARG_STR(0, "s", "S", .to = ARG_TO_STR(&s), .def = "dflt"),
		ARG_COUNT('v', "v", "V", .to = ARG_TO_INT(&cnt)),
		ARG_ENUM(0, "e", "E", ARG_CHOICES("x", "y"),
			 .to = ARG_TO_UINT(&u))) };
	struct fx f;

	fx_init(&f, spec);
	REQUIRE(PARSE(&f, "-b", "--i=-5", "--l=-6", "--i64=-7", "--u=8",
		      "--ul=9", "--u64=10", "--sz=2k", "--d=1.5", "--fl=2.5",
		      "-vvv") == ARG_OK);
	CHECK(b);
	CHECK(nb); /* unset: the variable keeps its value */
	CHECK_INT(i, -5);
	CHECK_INT(l, -6);
	CHECK_INT(i64, -7);
	CHECK_UINT(u, 8);
	CHECK_UINT(ul, 9);
	CHECK_UINT(u64, 10);
	CHECK_UINT(sz, 2000);
	CHECK_DBL(d, 1.5, 0);
	CHECK_DBL(fl, 2.5, 0);
	CHECK_STR(s, "dflt");
	CHECK_INT(cnt, 3);
	REQUIRE(PARSE(&f, "--e=y", "--nb=no") == ARG_OK);
	CHECK_UINT(u, 1);
	CHECK(!nb);
}

TEST(binding_ranges)
{
	int i = 0;
	unsigned u = 0;
	float fl = 0;
	int64_t i64 = 0;
	const arg_item spec[] = { ARG_APP(
		"t", "T", ARG_INT(0, "i", "I", .to = ARG_TO_INT(&i)),
		ARG_INT(0, "u", "U", .to = ARG_TO_UINT(&u)),
		ARG_DOUBLE(0, "fl", "F", .to = ARG_TO_FLOAT(&fl)),
		ARG_UINT(0, "big", "B", .to = ARG_TO_I64(&i64))) };
	char buf[64];
	struct fx f;

	fx_init(&f, spec);
	snprintf(buf, sizeof(buf), "--i=%d", INT_MAX);
	REQUIRE(PARSE(&f, buf) == ARG_OK);
	CHECK_INT(i, INT_MAX);
	snprintf(buf, sizeof(buf), "--i=%d", INT_MIN);
	REQUIRE(PARSE(&f, buf) == ARG_OK);
	CHECK_INT(i, INT_MIN);
	snprintf(buf, sizeof(buf), "--i=%lld", (long long)INT_MAX + 1);
	CHECK_INT(PARSE(&f, buf), ARG_E_RANGE);
	CHECK_STR(fx_error(&f)->detail, "does not fit the bound variable");
	CHECK_STRN(fx_error(&f)->text, fx_error(&f)->text_len, buf + 4);
	snprintf(buf, sizeof(buf), "--i=%lld", (long long)INT_MIN - 1);
	CHECK_INT(PARSE(&f, buf), ARG_E_RANGE);
	CHECK_INT(PARSE(&f, "--u=-1"), ARG_E_RANGE);
	REQUIRE(PARSE(&f, "--u=0") == ARG_OK);
	CHECK_INT(PARSE(&f, "--fl=1e300"), ARG_E_RANGE);
	REQUIRE(PARSE(&f, "--fl=inf") == ARG_OK);
	CHECK(fl > FLT_MAX);
	REQUIRE(PARSE(&f, "--i=-9223372036854775808") == ARG_E_RANGE);
	CHECK_INT(PARSE(&f, "--big=9223372036854775808"), ARG_E_RANGE);
	REQUIRE(PARSE(&f, "--big=9223372036854775807") == ARG_OK);
	CHECK_INT(i64, INT64_MAX);
}

TEST(binding_signs)
{
	unsigned long ul = 0;
	uint64_t u64 = 0;
	size_t sz = 0;
	long l = 0;
	const arg_item spec[] = { ARG_APP(
		"t", "T", ARG_INT(0, "ul", "", .to = ARG_TO_ULONG(&ul)),
		ARG_INT(0, "u64", "", .to = ARG_TO_U64(&u64)),
		ARG_INT(0, "sz", "", .to = ARG_TO_SIZE(&sz)),
		ARG_UINT(0, "l", "", .to = ARG_TO_LONG(&l))) };
	struct fx f;

	fx_init(&f, spec);
	CHECK_INT(PARSE(&f, "--ul=-1"), ARG_E_RANGE);
	CHECK_INT(PARSE(&f, "--u64=-1"), ARG_E_RANGE);
	CHECK_INT(PARSE(&f, "--sz=-1"), ARG_E_RANGE);
	CHECK_INT(PARSE(&f, "--l=18446744073709551615"), ARG_E_RANGE);
	REQUIRE(PARSE(&f, "--ul=1", "--u64=2", "--sz=3", "--l=4") == ARG_OK);
	CHECK_UINT(ul + u64 + sz, 6);
	CHECK_INT(l, 4);
}

TEST(check_without_value)
{
	const arg_item spec[] = { ARG_APP(
		"t", "T", ARG_INT(0, "lvl", "", .optional_value = 1,
				  .check = no_message)) };
	struct fx f;

	fx_init(&f, spec);
	CHECK_INT(PARSE(&f, "--lvl"), ARG_E_BAD_VALUE);
	CHECK(fx_error(&f)->text == NULL);
}

TEST(int64_extremes)
{
	int64_t i64 = 0;
	const arg_item spec[] = { ARG_APP(
		"t", "T", ARG_INT(0, "i", "I", .to = ARG_TO_I64(&i64))) };
	struct fx f;

	fx_init(&f, spec);
	REQUIRE(PARSE(&f, "--i=-9223372036854775808") == ARG_OK);
	CHECK(i64 == INT64_MIN);
	CHECK(arg_int(&f.p, "i") == INT64_MIN);
}

TEST_MAIN(TEST_CASE(lists), TEST_CASE(positionals),
	  TEST_CASE(variadic_minimum), TEST_CASE(typed_values),
	  TEST_CASE(custom_type), TEST_CASE(min_max),
	  TEST_CASE(check_functions), TEST_CASE(bindings),
	  TEST_CASE(binding_ranges), TEST_CASE(binding_signs),
	  TEST_CASE(check_without_value), TEST_CASE(int64_extremes))
