// SPDX-License-Identifier: GPL-2.0-only
/*
 * test_parse_sources.c - precedence CLI > sources > env > default,
 * .required and cfg.rule_sources (SDD 7.2 rule 15, 10.2)
 */
#include "parse_util.h"

static const arg_item app[] = { ARG_APP(
	"t", "T", ARG_INT('j', "jobs", "Jobs", .env = "T_JOBS", .def = "4"),
	ARG_STR(0, "user", "User", .env = "T_USER", .required = 1),
	ARG_FLAG('d', "debug", "Debug", .env = "T_DEBUG"),
	ARG_COUNT('v', "verbose", "Verbosity", .env = "T_VERBOSE"),
	ARG_STR(0, "tags", "Tags", .many = 1, .sep = ',', .env = "T_TAGS",
		.def = "a,b"),
	ARG_ARG_OPT("file", "File", .env = "T_FILE", .def = "-"),
	ARG_CMD("sub", ARG_NO_HANDLER, "Sub",
		ARG_INT(0, "level", "Level", .env = "T_LEVEL")),
	ARG_MAIN(ARG_NO_HANDLER)) };

/* A value source: "key=value" pairs in udata, by item key. */
struct src {
	const char *const *pairs;
	const arg_item *last_cmd;
	unsigned calls;
};

static const char *src_lookup(void *ud, const arg_item *cmd,
			      const arg_item *item, const char *key)
{
	struct src *s = ud;
	size_t i, n = strlen(key);

	(void)item;
	s->last_cmd = cmd;
	s->calls++;
	for (i = 0; s->pairs && s->pairs[i]; i++) {
		if (strncmp(s->pairs[i], key, n) == 0 && s->pairs[i][n] == '=')
			return s->pairs[i] + n + 1;
	}
	return NULL;
}

static const char *never(void *ud, const arg_item *cmd, const arg_item *item,
			 const char *key)
{
	(void)ud;
	(void)cmd;
	(void)item;
	(void)key;
	return NULL;
}

TEST(defaults)
{
	struct fx f;

	fx_init(&f, app);
	REQUIRE(PARSE(&f, "--user=me") == ARG_OK);
	CHECK_INT(arg_int(&f.p, "jobs"), 4);
	CHECK_INT(arg_source(&f.p, "jobs"), ARG_SRC_DEFAULT);
	CHECK(!arg_is_set(&f.p, "jobs"));
	CHECK_UINT(arg_count(&f.p, "tags"), 2);
	CHECK_STR(arg_str_at(&f.p, "tags", 1), "b");
	CHECK_STR(arg_str(&f.p, "file"), "-");
	CHECK(!arg_flag(&f.p, "debug"));
	CHECK_INT(arg_source(&f.p, "debug"), ARG_SRC_NONE);
	/* CLI lists replace the default list */
	REQUIRE(PARSE(&f, "--user=me", "--tags=x") == ARG_OK);
	CHECK_UINT(arg_count(&f.p, "tags"), 1);
}

TEST(environment)
{
	const struct fx_env env[] = {
		{ "T_JOBS", "8" },   { "T_USER", "env" },
		{ "T_DEBUG", "yes" }, { "T_VERBOSE", "2" },
		{ "T_TAGS", "p,q,r" }, { "T_FILE", "in.txt" },
		{ "T_LEVEL", "5" },   { NULL, NULL }
	};
	struct fx f;

	fx_config(&f, app);
	f.env = env;
	fx_start(&f);
	REQUIRE(PARSE0(&f) == ARG_OK);
	CHECK_INT(arg_int(&f.p, "jobs"), 8);
	CHECK_INT(arg_source(&f.p, "jobs"), ARG_SRC_ENV);
	CHECK(arg_is_set(&f.p, "jobs"));
	CHECK_STR(arg_str(&f.p, "user"), "env");
	CHECK(arg_flag(&f.p, "debug"));
	CHECK_INT(arg_int(&f.p, "verbose"), 2);
	CHECK_UINT(arg_count(&f.p, "tags"), 3);
	CHECK_STR(arg_str(&f.p, "file"), "in.txt");
	/* only items on the selected path fall back */
	REQUIRE(PARSE(&f, "sub") == ARG_OK);
	CHECK_INT(arg_int(&f.p, "level"), 5);
	/* the command line wins */
	REQUIRE(PARSE(&f, "-j1", "--user=cli", "-v", "x") == ARG_OK);
	CHECK_INT(arg_int(&f.p, "jobs"), 1);
	CHECK_INT(arg_source(&f.p, "jobs"), ARG_SRC_CLI);
	CHECK_STR(arg_str(&f.p, "user"), "cli");
	CHECK_INT(arg_int(&f.p, "verbose"), 1);
	CHECK_STR(arg_str(&f.p, "file"), "x");
}

TEST(bad_environment)
{
	const struct fx_env env[] = { { "T_JOBS", "many" },
				      { NULL, NULL } };
	const struct fx_env env2[] = { { "T_DEBUG", "perhaps" },
				       { NULL, NULL } };
	struct fx f;

	fx_config(&f, app);
	f.env = env;
	fx_start(&f);
	CHECK_INT(PARSE(&f, "--user=x"), ARG_E_BAD_VALUE);
	CHECK_INT(fx_error(&f)->argi, -1);
	CHECK_STR(fx_report(&f),
		  "prog: invalid value 'many' for '--jobs <N>': expected an "
		  "integer\nTry 'prog --help' for more information.\n");
	f.env = env2;
	CHECK_INT(PARSE(&f, "--user=x"), ARG_E_BAD_VALUE);
}

TEST(no_getenv_hook)
{
	struct fx f;

	fx_config(&f, app);
	f.cfg.getenv = NULL;
	fx_start(&f);
	/* the default hook reads the real environment */
	REQUIRE(PARSE(&f, "--user=x") == ARG_OK);
}

TEST(sources)
{
	const char *const pairs[] = { "jobs=16", "user=cfg",
				      "level=9", NULL };
	const struct fx_env env[] = { { "T_JOBS", "8" }, { NULL, NULL } };
	struct src s = { pairs, NULL, 0 };
	struct fx f;

	fx_config(&f, app);
	f.env = env;
	f.cfg.sources[0] = never;
	f.cfg.sources[2] = src_lookup;
	f.cfg.src_ud = &s;
	fx_start(&f);
	REQUIRE(PARSE0(&f) == ARG_OK);
	CHECK_INT(arg_int(&f.p, "jobs"), 16);
	CHECK_INT(arg_source(&f.p, "jobs"), ARG_SRC_SOURCE);
	CHECK(arg_is_set(&f.p, "jobs"));
	CHECK_STR(arg_str(&f.p, "user"), "cfg");
	CHECK_UINT(s.calls, 6);
	REQUIRE(PARSE(&f, "sub") == ARG_OK);
	CHECK_INT(arg_int(&f.p, "level"), 9);
	CHECK(s.last_cmd == &app[0].items[6]);
	REQUIRE(PARSE(&f, "-j2") == ARG_OK);
	CHECK_INT(arg_int(&f.p, "jobs"), 2);
}

TEST(source_order)
{
	const char *const first[] = { "jobs=1", NULL };
	const char *const second[] = { "jobs=2", "user=u", NULL };
	struct src s = { first, NULL, 0 };
	struct fx f;

	fx_config(&f, app);
	f.cfg.sources[0] = src_lookup;
	f.cfg.sources[1] = src_lookup;
	f.cfg.src_ud = &s;
	fx_start(&f);
	REQUIRE(PARSE(&f, "--user=x") == ARG_OK);
	CHECK_INT(arg_int(&f.p, "jobs"), 1);
	s.pairs = second;
	REQUIRE(PARSE0(&f) == ARG_OK);
	CHECK_INT(arg_int(&f.p, "jobs"), 2);
}

TEST(required)
{
	const struct fx_env env[] = { { "T_USER", "env" }, { NULL, NULL } };
	struct fx f;

	fx_init(&f, app);
	CHECK_INT(PARSE0(&f), ARG_E_MISSING_REQUIRED);
	CHECK(fx_error(&f)->item == &app[0].items[1]);
	CHECK_STR(fx_report(&f),
		  "prog: missing required option '--user'\n"
		  "Try 'prog --help' for more information.\n");
	/* a required option of an ancestor still counts below */
	CHECK_INT(PARSE(&f, "sub"), ARG_E_MISSING_REQUIRED);

	/* env satisfies .required by default ... */
	fx_config(&f, app);
	f.env = env;
	fx_start(&f);
	REQUIRE(PARSE0(&f) == ARG_OK);
	/* ... but not when rule_sources says only the CLI counts */
	fx_config(&f, app);
	f.env = env;
	f.cfg.rule_sources = ARG_SRC_BIT(ARG_SRC_CLI);
	fx_start(&f);
	CHECK_INT(PARSE0(&f), ARG_E_MISSING_REQUIRED);
	REQUIRE(PARSE(&f, "--user", "x") == ARG_OK);
}

TEST(required_by_default)
{
	const arg_item spec[] = { ARG_APP(
		"t", "T",
		ARG_STR(0, "user", "User", .required = 1, .def = "root")) };
	struct fx f;

	fx_init(&f, spec);
	CHECK_INT(PARSE0(&f), ARG_E_MISSING_REQUIRED);
	fx_config(&f, spec);
	f.cfg.rule_sources = ARG_RULE_SOURCES_DEFAULT |
			     ARG_SRC_BIT(ARG_SRC_DEFAULT);
	fx_start(&f);
	REQUIRE(PARSE0(&f) == ARG_OK);
	CHECK_STR(arg_str(&f.p, "user"), "root");
}

TEST_MAIN(TEST_CASE(defaults), TEST_CASE(environment),
	  TEST_CASE(bad_environment), TEST_CASE(no_getenv_hook),
	  TEST_CASE(sources), TEST_CASE(source_order), TEST_CASE(required),
	  TEST_CASE(required_by_default))
