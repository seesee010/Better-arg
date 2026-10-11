// SPDX-License-Identifier: GPL-2.0-only
/*
 * test_parse_getters.c - lookup keys and getter behaviour (SDD 5.3, 6.4)
 */
#include "parse_util.h"

static int run(arg *a)
{
	(void)a;
	return 0;
}

static const arg_item app[] = { ARG_APP(
	"t", "T", ARG_FLAG('v', "verbose", "V", .global = 1),
	ARG_FLAG('n', "dry-run", "Dry", .id = "dry"),
	ARG_FLAG('q', NULL, "Quiet"),
	ARG_STR('o', "out", "Out"),
	ARG_DOUBLE(0, "ratio", "R", .many = 1),
	ARG_COUNT('d', NULL, "Debug"),
	ARG_CUSTOM(0, "yes", &arg_t_bool, "Bool value"),
	ARG_CMD("sub", run, "Sub", ARG_STR(0, "out", "Shadowing out"),
		ARG_ARG("file", "File", .type = &arg_t_u16)),
	ARG_MAIN(run)) };

TEST(keys)
{
	struct fx f;

	fx_init(&f, app);
	REQUIRE(PARSE(&f, "-nq", "-o", "x", "-dd", "--yes=on") == ARG_OK);
	CHECK(arg_flag(&f.p, "dry"));
	CHECK(arg_flag(&f.p, "-q"));
	CHECK(arg_flag(&f.p, "yes"));
	CHECK_STR(arg_str(&f.p, "out"), "x");
	CHECK_INT(arg_int(&f.p, "-d"), 2);
	CHECK_UINT(arg_count(&f.p, "-d"), 2);
	CHECK_UINT(arg_count(&f.p, "dry"), 1);
	CHECK_UINT(arg_count(&f.p, "out"), 1);
	CHECK_UINT(arg_count(&f.p, "ratio"), 0);
	CHECK(f.err.len == 0);
}

TEST(lookup_chain)
{
	struct fx f;
	uint16_t file = 0;

	fx_init(&f, app);
	REQUIRE(PARSE(&f, "-o", "root", "sub", "--out", "sub", "8", "-v") ==
		ARG_OK);
	/* the deepest command first, then its ancestors */
	CHECK_STR(arg_str(&f.p, "out"), "sub");
	CHECK(arg_flag(&f.p, "verbose"));
	CHECK_UINT(arg_uint(&f.p, "file"), 8);
	CHECK(arg_get(&f.p, "file", &file));
	CHECK_UINT(file, 8);
	CHECK(arg_flag(&f.p, "sub"));
	CHECK_INT(arg_source(&f.p, "sub"), ARG_SRC_CLI);
	CHECK(f.err.len == 0);
}

TEST(lists_and_indexes)
{
	struct fx f;
	bool b = false;
	uint32_t level = 0;

	fx_init(&f, app);
	REQUIRE(PARSE(&f, "--ratio=0.5", "--ratio", "2", "-v", "-ddd") ==
		ARG_OK);
	CHECK_DBL(arg_double(&f.p, "ratio"), 0.5, 0);
	CHECK_DBL(arg_double_at(&f.p, "ratio", 1), 2, 0);
	CHECK_DBL(arg_double_at(&f.p, "ratio", 2), 0, 0);
	CHECK_INT(arg_int_at(&f.p, "-d", 1), 0);
	CHECK_UINT(arg_uint_at(&f.p, "-d", 1), 0);
	CHECK(arg_get(&f.p, "-d", &level));
	CHECK_UINT(level, 3);
	CHECK(arg_get(&f.p, "verbose", &b));
	CHECK(b);
	CHECK(arg_str_at(&f.p, "out", 1) == NULL);
	CHECK(arg_get_at(&f.p, "verbose", 0, &b));
	CHECK(!arg_get_at(&f.p, "verbose", 1, &b));
	CHECK(f.err.len == 0);
}

TEST(zero_values)
{
	struct fx f;
	double d = 1;

	fx_init(&f, app);
	REQUIRE(PARSE0(&f) == ARG_OK);
	CHECK(!arg_flag(&f.p, "verbose"));
	CHECK(arg_str(&f.p, "out") == NULL);
	CHECK_UINT(arg_count(&f.p, "out"), 0);
	CHECK(!arg_get(&f.p, "ratio", &d));
	CHECK_DBL(d, 1, 0);
	CHECK(!arg_is_set(&f.p, "out"));
	CHECK_INT(arg_source(&f.p, "out"), ARG_SRC_NONE);
	CHECK_INT(arg_int(&f.p, "-d"), 0);
	CHECK(!arg_flag(&f.p, "-d"));
	CHECK(f.err.len == 0);
}

static void check_diag(struct test_state *t_, struct fx *f,
		       const char *want)
{
#ifndef NDEBUG
	CHECK_STR(f->err.data, want);
#else
	(void)want;
	CHECK_UINT(f->err.len, 0);
#endif
	test_buf_reset(&f->err);
}

TEST(unknown_keys)
{
	struct fx f;
	int i;

	fx_init(&f, app);
	/* before any parse nothing is known */
	CHECK(!arg_flag(&f.p, "verbose"));
	check_diag(t_, &f, "arg: unknown key 'verbose' in 't'\n");
	REQUIRE(PARSE(&f, "sub", "1") == ARG_OK);
	arg__priv(&f.p)->diag_done = false;
	CHECK_INT(arg_int(&f.p, "nope"), 0);
	check_diag(t_, &f, "arg: unknown key 'nope' in 'prog sub'\n");
	/* once per parser */
	CHECK(arg_str(&f.p, "nope") == NULL);
	CHECK_INT(arg_enum(&f.p, "nope"), -1);
	CHECK_UINT(arg_uint(&f.p, "nope"), 0);
	CHECK_UINT(arg_uint_at(&f.p, "nope", 1), 0);
	CHECK_INT(arg_int_at(&f.p, "nope", 1), 0);
	CHECK_DBL(arg_double(&f.p, "nope"), 0, 0);
	CHECK_UINT(arg_count(&f.p, "nope"), 0);
	CHECK(!arg_get(&f.p, "nope", &i));
	CHECK(!arg_is_set(&f.p, "nope"));
	CHECK_INT(arg_source(&f.p, "nope"), ARG_SRC_NONE);
	CHECK(!arg_flag(&f.p, NULL));
	CHECK_UINT(f.err.len, 0);
	/* after arg_free() the values are gone */
	arg_free(&f.p);
	arg__priv(&f.p)->diag_done = false;
	CHECK(!arg_flag(&f.p, "verbose"));
	check_diag(t_, &f, "arg: unknown key 'verbose' in 'prog'\n");
}

TEST(wrong_getter)
{
	struct fx f;
	int x;

	fx_init(&f, app);
	REQUIRE(PARSE(&f, "-o", "x", "--ratio=1") == ARG_OK);
	CHECK_INT(arg_int(&f.p, "out"), 0);
	check_diag(t_, &f, "arg: wrong getter for key 'out' in 'prog'\n");
	arg__priv(&f.p)->diag_done = false;
	CHECK(!arg_flag(&f.p, "out"));
	check_diag(t_, &f, "arg: wrong getter for key 'out' in 'prog'\n");
	arg__priv(&f.p)->diag_done = false;
	CHECK(arg_str(&f.p, "ratio") == NULL);
	check_diag(t_, &f, "arg: wrong getter for key 'ratio' in 'prog'\n");
	arg__priv(&f.p)->diag_done = false;
	CHECK_UINT(arg_uint(&f.p, "ratio"), 0);
	check_diag(t_, &f, "arg: wrong getter for key 'ratio' in 'prog'\n");
	arg__priv(&f.p)->diag_done = false;
	CHECK_DBL(arg_double(&f.p, "out"), 0, 0);
	check_diag(t_, &f, "arg: wrong getter for key 'out' in 'prog'\n");
	arg__priv(&f.p)->diag_done = false;
	CHECK_INT(arg_enum(&f.p, "out"), -1);
	check_diag(t_, &f, "arg: wrong getter for key 'out' in 'prog'\n");
	arg__priv(&f.p)->diag_done = false;
	REQUIRE(PARSE(&f, "sub", "1") == ARG_OK);
	CHECK(!arg_get(&f.p, "sub", &x));
	check_diag(t_, &f, "arg: wrong getter for key 'sub' in 'prog sub'\n");
	/* the key is escaped */
	arg__priv(&f.p)->diag_done = false;
	CHECK(!arg_flag(&f.p, "\x1b"));
	check_diag(t_, &f, "arg: unknown key '\\x1B' in 'prog sub'\n");
}

static int fail_write(void *ud, const char *s, size_t n)
{
	(void)ud;
	(void)s;
	(void)n;
	return -1;
}

TEST(diag_write_error)
{
	struct fx f;

	fx_config(&f, app);
	f.cfg.write_err = fail_write;
	fx_start(&f);
	REQUIRE(PARSE0(&f) == ARG_OK);
	CHECK(!arg_flag(&f.p, "nope"));
}

TEST(udata_and_path)
{
	struct fx f;
	int marker;
	const char *p1;

	fx_config(&f, app);
	f.cfg.udata = &marker;
	fx_start(&f);
	CHECK(arg_udata(&f.p) == &marker);
	CHECK_STR(arg_path(&f.p), "t");
	CHECK(arg_path_is(&f.p, ""));
	REQUIRE(PARSE(&f, "sub", "3") == ARG_OK);
	p1 = arg_path(&f.p);
	CHECK_STR(p1, "prog sub");
	CHECK(arg_path(&f.p) == p1);
	CHECK(arg_path_is(&f.p, "sub"));
	CHECK(!arg_path_is(&f.p, ""));
}

TEST_MAIN(TEST_CASE(keys), TEST_CASE(lookup_chain),
	  TEST_CASE(lists_and_indexes), TEST_CASE(zero_values),
	  TEST_CASE(unknown_keys), TEST_CASE(wrong_getter),
	  TEST_CASE(diag_write_error), TEST_CASE(udata_and_path))
