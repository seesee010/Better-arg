// SPDX-License-Identifier: GPL-2.0-only
/*
 * test_parse_syntax.c - command line syntax: SDD 7.2 rules and the
 * 7.3 edge-case table (abbreviation and response files excluded)
 */
#include "parse_util.h"

static const arg_item app[] = { ARG_APP(
	"tool", "Test tool",
	ARG_COUNT('v', "verbose", "More output"),
	ARG_INT('j', "jobs", "Parallel jobs"),
	ARG_FLAG('x', "extract", "Extract"),
	ARG_STR('f', "file", "Archive"),
	ARG_STR('o', "out", "Output", .alias = "output"),
	ARG_STR('c', "color", "Colour", .optional_value = 1, .def = "auto",
		.alias = "colour"),
	ARG_FLAG('n', "net", "Network", .negatable = 1),
	ARG_STR(0, "name", "Name", .once = 1),
	ARG_ARGS_OPT("rest", "Everything else")) };

/* SDD 7.3 row 1: -vvv with v COUNT */
TEST(counter_bundle)
{
	struct fx f;

	fx_init(&f, app);
	REQUIRE(PARSE(&f, "-vvv") == ARG_OK);
	CHECK_INT(arg_int(&f.p, "verbose"), 3);
	CHECK_UINT(arg_count(&f.p, "verbose"), 3);
	CHECK_UINT(arg_uint(&f.p, "verbose"), 3);
	CHECK(arg_flag(&f.p, "verbose"));
	REQUIRE(PARSE(&f, "-v", "--verbose", "-vv") == ARG_OK);
	CHECK_INT(arg_int(&f.p, "verbose"), 4);
	REQUIRE(PARSE0(&f) == ARG_OK);
	CHECK_INT(arg_int(&f.p, "verbose"), 0);
	CHECK(!arg_flag(&f.p, "verbose"));
	CHECK_INT(arg_source(&f.p, "verbose"), ARG_SRC_NONE);
}

/* rows 2 and 3, rule 3: every way to give an integer value */
TEST(value_forms)
{
	static const struct {
		char *a, *b;
		int64_t want;
	} rows[] = {
		{ "-j4", NULL, 4 },	   { "-j", "4", 4 },
		{ "--jobs=4", NULL, 4 },   { "--jobs", "4", 4 },
		{ "-j", "-4", -4 },	   { "--jobs", "-4", -4 },
		{ "-j-4", NULL, -4 },	   { "--jobs=-4", NULL, -4 },
		{ "-j", "0x10", 16 },	   { "-vj", "7", 7 },
		{ "-vj7", NULL, 7 },	   { "--jobs", "--1", 0 },
	};
	struct fx f;
	size_t i;

	fx_init(&f, app);
	for (i = 0; i < ARG__COUNTOF(rows); i++) {
		char *argv[] = { "prog", rows[i].a, rows[i].b,
				 NULL };
		arg_status st = fx_parse(&f, argv);

		if (rows[i].want == 0) {
			CHECK_INT(st, ARG_E_BAD_VALUE);
			continue;
		}
		CHECK_INT(st, ARG_OK);
		CHECK_INT(arg_int(&f.p, "jobs"), rows[i].want);
		CHECK_INT(arg_source(&f.p, "jobs"), ARG_SRC_CLI);
	}
}

/* row 4 and rule 5: the first value-taking short eats the rest */
TEST(bundles)
{
	struct fx f;

	fx_init(&f, app);
	REQUIRE(PARSE(&f, "-xvf", "a.tar") == ARG_OK);
	CHECK(arg_flag(&f.p, "extract"));
	CHECK_INT(arg_int(&f.p, "verbose"), 1);
	CHECK_STR(arg_str(&f.p, "file"), "a.tar");
	CHECK_UINT(arg_count(&f.p, "rest"), 0);

	REQUIRE(PARSE(&f, "-xvfa.tar", "b") == ARG_OK);
	CHECK_STR(arg_str(&f.p, "file"), "a.tar");
	CHECK_STR(arg_str_at(&f.p, "rest", 0), "b");

	REQUIRE(PARSE(&f, "-vofile") == ARG_OK);
	CHECK_STR(arg_str(&f.p, "out"), "file");
	CHECK_INT(arg_int(&f.p, "verbose"), 1);

	/* a value-taking short at the end of a bundle takes the next arg */
	REQUIRE(PARSE(&f, "-xf", "-v") == ARG_OK);
	CHECK_STR(arg_str(&f.p, "file"), "-v");
	CHECK_INT(arg_int(&f.p, "verbose"), 0);
}

/* rows 5 and 6, rule 6 */
TEST(empty_values)
{
	struct fx f;
	const arg_error *e;

	fx_init(&f, app);
	REQUIRE(PARSE(&f, "--out=") == ARG_OK);
	CHECK_STR(arg_str(&f.p, "out"), "");
	CHECK(arg_is_set(&f.p, "out"));

	CHECK_INT(PARSE(&f, "--jobs="), ARG_E_BAD_VALUE);
	e = fx_error(&f);
	CHECK(e->item == &app[0].items[1]);
	CHECK_UINT(e->text_len, 0);
	CHECK_STR(e->detail, "expected an integer");
	CHECK_INT(e->argi, 1);
	CHECK_UINT(e->offset, 7);
	CHECK_STR(fx_report(&f),
		  "prog: invalid value '' for '--jobs <N>': expected an "
		  "integer\nTry 'prog --help' for more information.\n");

	CHECK_INT(PARSE(&f, "-j", "x1"), ARG_E_BAD_VALUE);
	e = fx_error(&f);
	CHECK_INT(e->argi, 2);
	CHECK_UINT(e->offset, 0);
	CHECK_STRN(e->text, e->text_len, "x1");
}

/* row 7, rule 1 */
TEST(dashdash)
{
	struct fx f;

	fx_init(&f, app);
	REQUIRE(PARSE(&f, "--", "-v", "--jobs=3", "--") == ARG_OK);
	CHECK_INT(arg_int(&f.p, "verbose"), 0);
	CHECK_UINT(arg_count(&f.p, "rest"), 3);
	CHECK_STR(arg_str_at(&f.p, "rest", 0), "-v");
	CHECK_STR(arg_str_at(&f.p, "rest", 1), "--jobs=3");
	CHECK_STR(arg_str_at(&f.p, "rest", 2), "--");
	CHECK(arg_str_at(&f.p, "rest", 3) == NULL);

	/* -h after -- is a positional, not help */
	REQUIRE(PARSE(&f, "-v", "--", "-h") == ARG_OK);
	CHECK_STR(arg_str_at(&f.p, "rest", 0), "-h");
}

/* row 10, rule 7: optional values bind only in attached form */
TEST(optional_values)
{
	struct fx f;

	fx_init(&f, app);
	REQUIRE(PARSE(&f, "--color", "red") == ARG_OK);
	CHECK_STR(arg_str(&f.p, "color"), "auto");
	CHECK_INT(arg_source(&f.p, "color"), ARG_SRC_CLI);
	CHECK_STR(arg_str_at(&f.p, "rest", 0), "red");

	REQUIRE(PARSE(&f, "--color=always") == ARG_OK);
	CHECK_STR(arg_str(&f.p, "color"), "always");
	REQUIRE(PARSE(&f, "--colour=never") == ARG_OK);
	CHECK_STR(arg_str(&f.p, "color"), "never");
	REQUIRE(PARSE(&f, "-calways") == ARG_OK);
	CHECK_STR(arg_str(&f.p, "color"), "always");
	REQUIRE(PARSE(&f, "-c", "red") == ARG_OK);
	CHECK_STR(arg_str(&f.p, "color"), "auto");
	CHECK_STR(arg_str_at(&f.p, "rest", 0), "red");
	REQUIRE(PARSE(&f, "-xc") == ARG_OK);
	CHECK_STR(arg_str(&f.p, "color"), "auto");
	CHECK(arg_flag(&f.p, "extract"));
	/* not given at all: default, but not "set" */
	REQUIRE(PARSE0(&f) == ARG_OK);
	CHECK_STR(arg_str(&f.p, "color"), "auto");
	CHECK_INT(arg_source(&f.p, "color"), ARG_SRC_DEFAULT);
	CHECK(!arg_is_set(&f.p, "color"));
}

/* optional value without a default stays unset but is "set" */
TEST(optional_value_without_default)
{
	const arg_item spec[] = { ARG_APP(
		"t", "T",
		ARG_INT('l', "level", "Level", .optional_value = 1)) };
	struct fx f;

	fx_init(&f, spec);
	REQUIRE(PARSE(&f, "--level") == ARG_OK);
	CHECK(arg_is_set(&f.p, "level"));
	CHECK_UINT(arg_count(&f.p, "level"), 0);
	CHECK_INT(arg_int(&f.p, "level"), 0);
	REQUIRE(PARSE(&f, "-l3") == ARG_OK);
	CHECK_INT(arg_int(&f.p, "level"), 3);
	CHECK_INT(PARSE(&f, "--level=x"), ARG_E_BAD_VALUE);
}

/* row 11, rule 9 */
TEST(negatable)
{
	const arg_item spec[] = { ARG_APP(
		"t", "T", ARG_FLAG(0, "cache", "Cache", .negatable = 1),
		ARG_FLAG(0, "no-cache", "Explicit"),
		ARG_FLAG('n', "net", "Network", .negatable = 1,
			 .alias = "network")) };
	struct fx f;

	fx_init(&f, app);
	REQUIRE(PARSE(&f, "--no-net") == ARG_OK);
	CHECK(!arg_flag(&f.p, "net"));
	CHECK(arg_is_set(&f.p, "net"));
	REQUIRE(PARSE(&f, "--no-net", "--net") == ARG_OK);
	CHECK(arg_flag(&f.p, "net"));
	REQUIRE(PARSE(&f, "--net", "--no-net") == ARG_OK);
	CHECK(!arg_flag(&f.p, "net"));
	CHECK_INT(PARSE(&f, "--no-net=yes"), ARG_E_UNEXPECTED_VALUE);
	CHECK_INT(PARSE(&f, "--no-extract"), ARG_E_UNKNOWN_OPTION);

	/* an explicitly declared --no-name wins over the generated one */
	fx_init(&f, spec);
	REQUIRE(PARSE(&f, "--no-cache") == ARG_OK);
	CHECK(arg_flag(&f.p, "no-cache"));
	CHECK_INT(arg_source(&f.p, "cache"), ARG_SRC_NONE);
	REQUIRE(PARSE(&f, "--no-net") == ARG_OK);
	CHECK(!arg_flag(&f.p, "net"));
	REQUIRE(PARSE(&f, "--network") == ARG_OK);
	CHECK(arg_flag(&f.p, "net"));
	/* no "no-" form for the alias */
	CHECK_INT(PARSE(&f, "--no-network"), ARG_E_UNKNOWN_OPTION);
}

/* row 12, rule 4 */
TEST(negative_numbers)
{
	const arg_item num[] = { ARG_APP(
		"t", "T", ARG_FLAG('v', "verbose", "V"),
		ARG_ARG("n", "Number", .type = &arg_t_i64)) };
	const arg_item digit[] = { ARG_APP(
		"t", "T", ARG_FLAG('5', "five", "Five"),
		ARG_ARG_OPT("n", "Number", .type = &arg_t_i64)) };
	const arg_item sub_digit[] = { ARG_APP(
		"t", "T", ARG_FLAG('1', "one", "One", .global = 1),
		ARG_FLAG('2', "two", "Local digit"),
		ARG_CMD("run", ARG_NO_HANDLER, "Run", ARG_ARG_OPT("n", "N",
						    .type = &arg_t_i64))) };
	struct fx f;

	fx_init(&f, num);
	REQUIRE(PARSE(&f, "-5") == ARG_OK);
	CHECK_INT(arg_int(&f.p, "n"), -5);
	REQUIRE(PARSE(&f, "-v", "-12") == ARG_OK);
	CHECK_INT(arg_int(&f.p, "n"), -12);

	fx_init(&f, digit);
	REQUIRE(PARSE(&f, "-5") == ARG_OK);
	CHECK(arg_flag(&f.p, "five"));
	CHECK_UINT(arg_count(&f.p, "n"), 0);
	CHECK_INT(PARSE(&f, "-7"), ARG_E_UNKNOWN_OPTION);

	/* a global digit option of an ancestor counts, a local one not */
	fx_init(&f, sub_digit);
	CHECK_INT(PARSE(&f, "run", "-3"), ARG_E_UNKNOWN_OPTION);
	REQUIRE(PARSE(&f, "run", "-1") == ARG_OK);
	CHECK(arg_flag(&f.p, "one"));
}

/* row 13, rule 2 */
TEST(lone_dash)
{
	struct fx f;

	fx_init(&f, app);
	REQUIRE(PARSE(&f, "-", "-f", "-") == ARG_OK);
	CHECK_STR(arg_str_at(&f.p, "rest", 0), "-");
	CHECK_STR(arg_str(&f.p, "file"), "-");
}

/* rule 8 */
TEST(flag_values)
{
	static const char *const yes[] = { "yes", "true", "on", "1", "YES" };
	static const char *const no[] = { "no", "false", "off", "0" };
	char buf[32];
	struct fx f;
	size_t i;

	fx_init(&f, app);
	for (i = 0; i < ARG__COUNTOF(yes); i++) {
		snprintf(buf, sizeof(buf), "--extract=%s", yes[i]);
		REQUIRE(PARSE(&f, buf) == ARG_OK);
		CHECK(arg_flag(&f.p, "extract"));
	}
	for (i = 0; i < ARG__COUNTOF(no); i++) {
		snprintf(buf, sizeof(buf), "--extract=%s", no[i]);
		REQUIRE(PARSE(&f, buf) == ARG_OK);
		CHECK(!arg_flag(&f.p, "extract"));
		CHECK(arg_is_set(&f.p, "extract"));
	}
	CHECK_INT(PARSE(&f, "--extract=maybe"), ARG_E_BAD_VALUE);
	CHECK_INT(PARSE(&f, "--verbose=1"), ARG_E_UNEXPECTED_VALUE);
	CHECK(fx_error(&f)->item == &app[0].items[0]);
	CHECK_STR(fx_report(&f),
		  "prog: option '--verbose' doesn't take a value\n"
		  "Try 'prog --help' for more information.\n");
	/* a bool short flag never takes a value: "-xyes" is a bundle */
	CHECK_INT(PARSE(&f, "-xyes"), ARG_E_UNKNOWN_OPTION);
}

/* rule 10 */
TEST(repeats)
{
	struct fx f;

	fx_init(&f, app);
	REQUIRE(PARSE(&f, "-j1", "--jobs=2", "-j", "3") == ARG_OK);
	CHECK_INT(arg_int(&f.p, "jobs"), 3);
	CHECK_UINT(arg_count(&f.p, "jobs"), 1);
	REQUIRE(PARSE(&f, "--name", "a") == ARG_OK);
	CHECK_INT(PARSE(&f, "--name", "a", "--name=b"), ARG_E_REPEATED);
	CHECK_INT(fx_error(&f)->argi, 3);
	CHECK_STR(fx_report(&f),
		  "prog: option '--name' may only be given once\n"
		  "Try 'prog --help' for more information.\n");
}

TEST(missing_values)
{
	struct fx f;

	fx_init(&f, app);
	CHECK_INT(PARSE(&f, "-j"), ARG_E_MISSING_VALUE);
	CHECK_STRN(fx_error(&f)->text, fx_error(&f)->text_len, "-j");
	CHECK_INT(PARSE(&f, "-vj"), ARG_E_MISSING_VALUE);
	CHECK_UINT(fx_error(&f)->offset, 2);
	CHECK_INT(PARSE(&f, "--jobs"), ARG_E_MISSING_VALUE);
	CHECK_STRN(fx_error(&f)->text, fx_error(&f)->text_len, "--jobs");
	CHECK_STR(fx_report(&f),
		  "prog: option '--jobs' requires a value\n"
		  "Try 'prog --help' for more information.\n");
}

/* rule 3, second sentence */
TEST(strict_values)
{
	struct fx f;

	fx_init(&f, app);
	REQUIRE(PARSE(&f, "-o", "-v") == ARG_OK);
	CHECK_STR(arg_str(&f.p, "out"), "-v");

	fx_config(&f, app);
	f.cfg.style = ARG_STRICT_VALUES;
	f.cfg.version_text = "1.0";
	fx_start(&f);
	CHECK_INT(PARSE(&f, "-o", "-v"), ARG_E_MISSING_VALUE);
	CHECK_INT(PARSE(&f, "-o", "--jobs=2"), ARG_E_MISSING_VALUE);
	CHECK_INT(PARSE(&f, "--out", "--no-net"), ARG_E_MISSING_VALUE);
	CHECK_INT(PARSE(&f, "--out", "--"), ARG_E_MISSING_VALUE);
	CHECK_INT(PARSE(&f, "--out", "-h"), ARG_E_MISSING_VALUE);
	CHECK_INT(PARSE(&f, "--out", "--help"), ARG_E_MISSING_VALUE);
	CHECK_INT(PARSE(&f, "--out", "-V"), ARG_E_MISSING_VALUE);
	CHECK_INT(PARSE(&f, "--out", "--version"), ARG_E_MISSING_VALUE);
	/* unknown options, negative numbers, attached forms are fine */
	REQUIRE(PARSE(&f, "-o", "-q") == ARG_OK);
	CHECK_STR(arg_str(&f.p, "out"), "-q");
	REQUIRE(PARSE(&f, "-o", "--quux") == ARG_OK);
	REQUIRE(PARSE(&f, "-o", "-5") == ARG_OK);
	CHECK_STR(arg_str(&f.p, "out"), "-5");
	REQUIRE(PARSE(&f, "-o-v") == ARG_OK);
	CHECK_STR(arg_str(&f.p, "out"), "-v");
	REQUIRE(PARSE(&f, "--out=-v") == ARG_OK);
	REQUIRE(PARSE(&f, "-o", "-") == ARG_OK);
	CHECK_STR(arg_str(&f.p, "out"), "-");
}

/* rule 14 */
TEST(unknown_options)
{
	struct fx f;
	const arg_error *e;

	fx_init(&f, app);
	CHECK_INT(PARSE(&f, "--job=3"), ARG_E_UNKNOWN_OPTION);
	e = fx_error(&f);
	CHECK_STRN(e->text, e->text_len, "--job");
	CHECK_STR(e->suggestion, "jobs");
	CHECK_STR(e->suggestion_prefix, "--");
	CHECK(e->item == NULL);
	CHECK(e->cmd == &app[0]);
	CHECK_STR(fx_report(&f), "prog: unknown option '--job'\n"
				 "  Did you mean '--jobs'?\n"
				 "Try 'prog --help' for more information.\n");

	CHECK_INT(PARSE(&f, "--colur"), ARG_E_UNKNOWN_OPTION);
	CHECK_STR(fx_error(&f)->suggestion, "color");
	CHECK_INT(PARSE(&f, "--hepl"), ARG_E_UNKNOWN_OPTION);
	CHECK_STR(fx_error(&f)->suggestion, "help");
	CHECK_INT(PARSE(&f, "--zzzzzz"), ARG_E_UNKNOWN_OPTION);
	CHECK(fx_error(&f)->suggestion == NULL);
	CHECK_INT(PARSE(&f, "--"), ARG_OK);
	CHECK_INT(PARSE(&f, "--=x"), ARG_E_UNKNOWN_OPTION);

	CHECK_INT(PARSE(&f, "-vq"), ARG_E_UNKNOWN_OPTION);
	e = fx_error(&f);
	CHECK_STRN(e->text, e->text_len, "-q");
	CHECK(e->suggestion == NULL);
	CHECK_UINT(e->offset, 2);
	CHECK_STR(fx_report(&f), "prog: unknown option '-q'\n"
				 "Try 'prog --help' for more information.\n");
	/* the tokenizer passes '-' and non-ASCII through as short chars */
	CHECK_INT(PARSE(&f, "-v-x"), ARG_E_UNKNOWN_OPTION);
	CHECK_STRN(fx_error(&f)->text, 2, "--");
	CHECK_INT(PARSE(&f, "-\xc3\xa9"), ARG_E_UNKNOWN_OPTION);
	CHECK_STR(fx_report(&f), "prog: unknown option '-\\xC3'\n"
				 "Try 'prog --help' for more information.\n");
}

/* rule 14, second half: ARG_ALLOW_UNKNOWN */
TEST(allow_unknown)
{
	struct fx f;

	fx_config(&f, app);
	f.cfg.style = ARG_ALLOW_UNKNOWN;
	fx_start(&f);
	REQUIRE(PARSE(&f, "--foo=1", "-q", "-vz", "--bar", "x", "-qv",
		      "-v") == ARG_OK);
	CHECK_UINT(arg_rest_count(&f.p), 5);
	CHECK_STR(arg_rest_at(&f.p, 0), "--foo=1");
	CHECK_STR(arg_rest_at(&f.p, 1), "-q");
	CHECK_STR(arg_rest_at(&f.p, 2), "-z");
	CHECK_STR(arg_rest_at(&f.p, 3), "--bar");
	CHECK_STR(arg_rest_at(&f.p, 4), "-qv");
	CHECK(arg_rest_at(&f.p, 5) == NULL);
	CHECK_INT(arg_int(&f.p, "verbose"), 2);
	CHECK_STR(arg_str_at(&f.p, "rest", 0), "x");
	/* a new parse starts over */
	REQUIRE(PARSE0(&f) == ARG_OK);
	CHECK_UINT(arg_rest_count(&f.p), 0);
}

/* rule 11 */
TEST(posix_ordering)
{
	static const struct fx_env env[] = { { "POSIXLY_CORRECT", "1" },
					     { NULL, NULL } };
	struct fx f;

	fx_init(&f, app);
	REQUIRE(PARSE(&f, "a", "-v", "b") == ARG_OK);
	CHECK_INT(arg_int(&f.p, "verbose"), 1);
	CHECK_UINT(arg_count(&f.p, "rest"), 2);

	fx_config(&f, app);
	f.cfg.style = ARG_STYLE_POSIX;
	fx_start(&f);
	REQUIRE(PARSE(&f, "-v", "a", "-v", "--jobs=1") == ARG_OK);
	CHECK_INT(arg_int(&f.p, "verbose"), 1);
	CHECK_UINT(arg_count(&f.p, "rest"), 3);
	CHECK_STR(arg_str_at(&f.p, "rest", 1), "-v");
	CHECK_INT(arg_source(&f.p, "jobs"), ARG_SRC_NONE);

	/* POSIXLY_CORRECT counts only when honoured */
	fx_config(&f, app);
	f.env = env;
	fx_start(&f);
	REQUIRE(PARSE(&f, "a", "-v") == ARG_OK);
	CHECK_INT(arg_int(&f.p, "verbose"), 1);
	fx_config(&f, app);
	f.env = env;
	f.cfg.style = ARG_HONOR_POSIXLY_CORRECT;
	fx_start(&f);
	REQUIRE(PARSE(&f, "a", "-v") == ARG_OK);
	CHECK_INT(arg_int(&f.p, "verbose"), 0);
	f.env = NULL;
	REQUIRE(PARSE(&f, "a", "-v") == ARG_OK);
	CHECK_INT(arg_int(&f.p, "verbose"), 1);
}

/* rule 16: values are bytes */
TEST(bytes)
{
	struct fx f;

	fx_init(&f, app);
	REQUIRE(PARSE(&f, "-o", "\xff\x01\xc3\xa9", "--file=a=b") == ARG_OK);
	CHECK_STR(arg_str(&f.p, "out"), "\xff\x01\xc3\xa9");
	CHECK_STR(arg_str(&f.p, "file"), "a=b");
	CHECK_INT(PARSE(&f, "-j", "\x1b[31m"), ARG_E_BAD_VALUE);
	CHECK_STR(fx_report(&f),
		  "prog: invalid value '\\x1B[31m' for '--jobs <N>': "
		  "expected an integer\n"
		  "Try 'prog --help' for more information.\n");
}

TEST(aliases)
{
	struct fx f;

	fx_init(&f, app);
	REQUIRE(PARSE(&f, "--output", "x") == ARG_OK);
	CHECK_STR(arg_str(&f.p, "out"), "x");
}

TEST(auto_help_and_version)
{
	const arg_item own[] = { ARG_APP(
		"t", "T", ARG_FLAG('h', "host", "Own -h"),
		ARG_FLAG('V', NULL, "Own -V"),
		ARG_STR(0, "help", "Own --help")) };
	struct fx f;

	fx_init(&f, app);
	CHECK_INT(PARSE(&f, "-h"), ARG_EXIT_HELP);
	CHECK_INT(PARSE(&f, "-vh", "--bogus"), ARG_EXIT_HELP);
	CHECK_INT(PARSE(&f, "a", "--help", "--bogus"), ARG_EXIT_HELP);
	CHECK_INT(PARSE(&f, "--help=x"), ARG_EXIT_HELP);
	CHECK_INT(fx_error(&f)->status, ARG_EXIT_HELP);
	/* an earlier error wins */
	CHECK_INT(PARSE(&f, "--bogus", "-h"), ARG_E_UNKNOWN_OPTION);
	/* version only with a version text */
	CHECK_INT(PARSE(&f, "-V"), ARG_E_UNKNOWN_OPTION);
	CHECK_INT(PARSE(&f, "--version"), ARG_E_UNKNOWN_OPTION);

	fx_config(&f, app);
	f.cfg.version_text = "tool 1.2";
	fx_start(&f);
	CHECK_INT(PARSE(&f, "-V"), ARG_EXIT_VERSION);
	CHECK_INT(PARSE(&f, "--version", "-h"), ARG_EXIT_VERSION);
	test_buf_reset(&f.out);
	CHECK_INT(arg_report(&f.p), 0);
	CHECK_STR(f.out.data, "tool 1.2\n");
	CHECK_INT(PARSE(&f, "--verison"), ARG_E_UNKNOWN_OPTION);
	CHECK_STR(fx_error(&f)->suggestion, "version");

	fx_config(&f, app);
	f.cfg.style = ARG_NO_AUTO_HELP;
	fx_start(&f);
	CHECK_INT(PARSE(&f, "-h"), ARG_E_UNKNOWN_OPTION);
	CHECK_INT(PARSE(&f, "--help"), ARG_E_UNKNOWN_OPTION);

	/* the spec's own -h, -V and --help win */
	fx_config(&f, own);
	f.cfg.version_text = "own 1";
	fx_start(&f);
	REQUIRE(PARSE(&f, "-h", "-V", "--help", "x") == ARG_OK);
	CHECK(arg_flag(&f.p, "host"));
	CHECK(arg_flag(&f.p, "-V"));
	CHECK_STR(arg_str(&f.p, "help"), "x");
	CHECK_INT(PARSE(&f, "--version"), ARG_EXIT_VERSION);
}

/* NFR-04: argv is never modified */
TEST(argv_untouched)
{
	char a0[] = "/usr/bin/tool", a1[] = "-xvfa.tar", a2[] = "--jobs=4",
	     a3[] = "--color=red", a4[] = "--", a5[] = "-v";
	char *argv[] = { a0, a1, a2, a3, a4, a5, NULL };
	char *copy[ARG__COUNTOF(argv)];
	char text[6][16];
	struct fx f;
	size_t i;

	for (i = 0; argv[i]; i++) {
		copy[i] = argv[i];
		snprintf(text[i], sizeof(text[i]), "%s", argv[i]);
	}
	fx_init(&f, app);
	REQUIRE(fx_parse(&f, argv) == ARG_OK);
	for (i = 0; argv[i]; i++) {
		CHECK(copy[i] == argv[i]);
		CHECK_STR(argv[i], text[i]);
	}
	CHECK(arg_str(&f.p, "file") == a1 + 4);
	CHECK(arg_str(&f.p, "color") == a3 + 8);
}

/* SDD 15: malformed argv */
TEST(malformed_argv)
{
	char a0[] = "tool", a1[] = "-v", a2[] = "-v";
	char *argv[] = { a0, a1, NULL, a2, NULL };
	char *nul0[] = { NULL, a1, NULL };
	char *empty0[] = { "", a1, NULL };
	struct fx f;

	fx_init(&f, app);
	CHECK_INT(arg_parse(&f.p, -1, argv), ARG_OK);
	CHECK_INT(arg_int(&f.p, "verbose"), 0);
	CHECK_STR(arg_path(&f.p), "tool");
	CHECK_INT(arg_parse(&f.p, 4, argv), ARG_OK);
	CHECK_INT(arg_int(&f.p, "verbose"), 1);
	CHECK_INT(arg_parse(&f.p, 0, argv), ARG_OK);
	CHECK_STR(arg_path(&f.p), "tool");
	CHECK_INT(arg_parse(&f.p, 2, NULL), ARG_OK);
	CHECK_STR(arg_path(&f.p), "tool");
	CHECK_INT(arg_parse(&f.p, 2, nul0), ARG_OK);
	CHECK_INT(arg_int(&f.p, "verbose"), 0);
	CHECK_STR(arg_path(&f.p), "tool");
	CHECK_INT(arg_parse(&f.p, 2, empty0), ARG_OK);
	CHECK_INT(arg_int(&f.p, "verbose"), 1);
	CHECK_STR(arg_path(&f.p), "tool");
	CHECK_INT(arg_parse(&f.p, 2, (char *[]){ "/a/b/", NULL }), ARG_OK);
	CHECK_STR(arg_path(&f.p), "b/");
	CHECK_INT(arg_parse(&f.p, 1, (char *[]){ "./x/run", NULL }), ARG_OK);
	CHECK_STR(arg_path(&f.p), "run");
}

TEST_MAIN(TEST_CASE(counter_bundle), TEST_CASE(value_forms),
	  TEST_CASE(bundles), TEST_CASE(empty_values), TEST_CASE(dashdash),
	  TEST_CASE(optional_values), TEST_CASE(optional_value_without_default),
	  TEST_CASE(negatable), TEST_CASE(negative_numbers),
	  TEST_CASE(lone_dash), TEST_CASE(flag_values), TEST_CASE(repeats),
	  TEST_CASE(missing_values), TEST_CASE(strict_values),
	  TEST_CASE(unknown_options), TEST_CASE(allow_unknown),
	  TEST_CASE(posix_ordering), TEST_CASE(bytes), TEST_CASE(aliases),
	  TEST_CASE(auto_help_and_version), TEST_CASE(argv_untouched),
	  TEST_CASE(malformed_argv))
