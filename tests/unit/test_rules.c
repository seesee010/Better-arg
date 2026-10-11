// SPDX-License-Identifier: GPL-2.0-only
/*
 * test_rules.c - command rules: MUTEX, ONE_OF, ANY_OF, REQUIRES,
 * CONFLICTS, RULE_FN, "set" sources and spec checks (SDD 10)
 */
#include "parse_util.h"

/* ------------------------------------------------------------------ */
/* Fixtures                                                           */
/* ------------------------------------------------------------------ */

static int nop(arg *a)
{
	(void)a;
	return 0;
}

static int do_list(arg *a)
{
	(void)a;
	return 3;
}

ARG_DEFINE(color_opts, ARG_FLAG(0, "color", "Color"),
	   ARG_FLAG(0, "mono", "Monochrome"));

static const arg_item app[] = { ARG_APP(
	"pkg", "Package tool", ARG_MAIN(nop),
	ARG_FLAG(0, "verbose", "Verbose", .global = 1),
	ARG_FLAG('q', "quiet", "Quiet"),
	ARG_CONFLICTS("quiet", "verbose"),
	ARG_CMD("export", nop, "Export",
		ARG_FLAG(0, "json", "JSON"), ARG_FLAG(0, "yaml", "YAML"),
		ARG_FLAG(0, "toml", "TOML"), ARG_STR(0, "user", "User"),
		ARG_STR('p', "password", "Password", .alias = "pw"),
		ARG_STR(0, "host", "Host"),
		ARG_MUTEX("json", "yaml", "toml"),
		ARG_REQUIRES("user", "pw", "host")),
	ARG_CMD("in", nop, "Input", ARG_STR(0, "file", "File"),
		ARG_FLAG(0, "stdin", "Stdin"), ARG_FLAG(0, "net", "Net"),
		ARG_ONE_OF("file", "stdin", "net")),
	ARG_CMD("edit", nop, "Edit", ARG_STR(0, "add", "Add"),
		ARG_STR(0, "remove", "Remove"),
		ARG_ANY_OF("add", "remove")),
	ARG_CMD("short", nop, "Short only", ARG_FLAG('x', NULL, "X"),
		ARG_FLAG('y', NULL, "Y"), ARG_COUNT('v', "level", "Lvl"),
		ARG_MUTEX("-x", "-y"), ARG_CONFLICTS("-v", "-x")),
	ARG_CMD("use", nop, "Use", ARG_USE(color_opts),
		ARG_MUTEX("color", "mono")),
	ARG_CMD("src", nop, "Sources",
		ARG_STR(0, "a", "A", .env = "RULE_A"),
		ARG_STR(0, "b", "B", .def = "x"),
		ARG_MUTEX("a", "b")),
	ARG_CMD("parent", nop, "Parent", ARG_FLAG(0, "dry", "Dry"),
		ARG_FLAG(0, "force", "Force"),
		ARG_CONFLICTS("dry", "force"),
		ARG_CMD("child", nop, "Child", ARG_FLAG(0, "one", "One"),
			ARG_FLAG(0, "two", "Two"),
			ARG_MUTEX("one", "two")))) };

static const char *report(struct fx *f)
{
	return fx_report(f);
}

/* ------------------------------------------------------------------ */
/* MUTEX                                                              */
/* ------------------------------------------------------------------ */

TEST(mutex_ok)
{
	struct fx f;

	fx_init(&f, app);
	CHECK_INT(PARSE(&f, "export"), ARG_OK);
	CHECK_INT(PARSE(&f, "export", "--json"), ARG_OK);
	CHECK_INT(PARSE(&f, "export", "--toml"), ARG_OK);
	/* the same option twice is still one option */
	CHECK_INT(PARSE(&f, "export", "--yaml", "--yaml"), ARG_OK);
}

TEST(mutex_violation)
{
	struct fx f;

	fx_init(&f, app);
	CHECK_INT(PARSE(&f, "export", "--json", "--yaml"), ARG_E_RULE);
	CHECK_STR(fx_error(&f)->item->name, "yaml");
	CHECK_INT(fx_error(&f)->argi, -1);
	CHECK_STR(fx_error(&f)->cmd->name, "export");
	CHECK_STR(fx_error(&f)->detail,
		  "options '--json' and '--yaml' cannot be used together");
	CHECK_STR(report(&f),
		  "prog export: options '--json' and '--yaml' cannot be used "
		  "together\n"
		  "Try 'prog export --help' for more information.\n");
	CHECK_INT(arg_report(&f.p), 2);

	/* the first two set members, in declaration order of the rule */
	CHECK_INT(PARSE(&f, "export", "--toml", "--yaml", "--json"),
		  ARG_E_RULE);
	CHECK_STR(fx_error(&f)->detail,
		  "options '--json' and '--yaml' cannot be used together");
	CHECK_INT(PARSE(&f, "export", "--toml", "--yaml"), ARG_E_RULE);
	CHECK_STR(fx_error(&f)->detail,
		  "options '--yaml' and '--toml' cannot be used together");
	CHECK_STR(fx_error(&f)->item->name, "toml");
}

/* ------------------------------------------------------------------ */
/* ONE_OF                                                             */
/* ------------------------------------------------------------------ */

TEST(one_of_ok)
{
	struct fx f;

	fx_init(&f, app);
	CHECK_INT(PARSE(&f, "in", "--file", "a"), ARG_OK);
	CHECK_INT(PARSE(&f, "in", "--stdin"), ARG_OK);
	CHECK_INT(PARSE(&f, "in", "--net"), ARG_OK);
}

TEST(one_of_none)
{
	struct fx f;

	fx_init(&f, app);
	CHECK_INT(PARSE(&f, "in"), ARG_E_RULE);
	CHECK_STR(fx_error(&f)->item->name, "file");
	CHECK_STR(report(&f),
		  "prog in: exactly one of '--file', '--stdin', '--net' is "
		  "required\n"
		  "Try 'prog in --help' for more information.\n");
}

TEST(one_of_several)
{
	struct fx f;

	fx_init(&f, app);
	CHECK_INT(PARSE(&f, "in", "--stdin", "--file=x"), ARG_E_RULE);
	CHECK_STR(fx_error(&f)->detail,
		  "options '--file' and '--stdin' cannot be used together");
	CHECK_STR(fx_error(&f)->item->name, "stdin");
	CHECK_INT(PARSE(&f, "in", "--net", "--stdin"), ARG_E_RULE);
	CHECK_STR(fx_error(&f)->detail,
		  "options '--stdin' and '--net' cannot be used together");
}

/* ------------------------------------------------------------------ */
/* ANY_OF                                                             */
/* ------------------------------------------------------------------ */

TEST(any_of)
{
	struct fx f;

	fx_init(&f, app);
	CHECK_INT(PARSE(&f, "edit", "--add", "a"), ARG_OK);
	CHECK_INT(PARSE(&f, "edit", "--remove", "a"), ARG_OK);
	CHECK_INT(PARSE(&f, "edit", "--remove", "a", "--add", "b"), ARG_OK);
	CHECK_INT(PARSE(&f, "edit"), ARG_E_RULE);
	CHECK_STR(fx_error(&f)->item->name, "add");
	CHECK_STR(report(&f),
		  "prog edit: at least one of '--add', '--remove' is "
		  "required\n"
		  "Try 'prog edit --help' for more information.\n");
}

/* ------------------------------------------------------------------ */
/* REQUIRES                                                           */
/* ------------------------------------------------------------------ */

TEST(requires_ok)
{
	struct fx f;

	fx_init(&f, app);
	/* not triggered */
	CHECK_INT(PARSE(&f, "export"), ARG_OK);
	CHECK_INT(PARSE(&f, "export", "--pw", "s"), ARG_OK);
	CHECK_INT(PARSE(&f, "export", "--user", "u", "-p", "s", "--host",
			"h"),
		  ARG_OK);
}

TEST(requires_violation)
{
	struct fx f;

	fx_init(&f, app);
	CHECK_INT(PARSE(&f, "export", "--user", "u"), ARG_E_RULE);
	/* member named by alias, label uses the long name */
	CHECK_STR(fx_error(&f)->item->name, "password");
	CHECK_STR(report(&f),
		  "prog export: option '--user' requires '--password'\n"
		  "Try 'prog export --help' for more information.\n");
	/* the first missing one */
	CHECK_INT(PARSE(&f, "export", "--user", "u", "--password", "p"),
		  ARG_E_RULE);
	CHECK_STR(fx_error(&f)->detail,
		  "option '--user' requires '--host'");
	CHECK_STR(fx_error(&f)->item->name, "host");
}

/* ------------------------------------------------------------------ */
/* CONFLICTS                                                          */
/* ------------------------------------------------------------------ */

TEST(conflicts)
{
	struct fx f;

	fx_init(&f, app);
	CHECK_INT(PARSE(&f, "--quiet"), ARG_OK);
	CHECK_INT(PARSE(&f, "--verbose"), ARG_OK);
	CHECK_INT(PARSE(&f, "--quiet", "--verbose"), ARG_E_RULE);
	CHECK_STR(fx_error(&f)->item->name, "verbose");
	CHECK_STR(report(&f),
		  "prog: option '--quiet' conflicts with '--verbose'\n"
		  "Try 'prog --help' for more information.\n");
}

/* ------------------------------------------------------------------ */
/* Nested commands                                                    */
/* ------------------------------------------------------------------ */

TEST(parent_rules_apply_to_children)
{
	struct fx f;

	fx_init(&f, app);
	CHECK_INT(PARSE(&f, "-q", "export", "--verbose"), ARG_E_RULE);
	CHECK_STR(report(&f),
		  "prog export: option '--quiet' conflicts with '--verbose'\n"
		  "Try 'prog export --help' for more information.\n");

	CHECK_INT(PARSE(&f, "parent", "--dry", "--force", "child"),
		  ARG_E_RULE);
	CHECK_STR(report(&f),
		  "prog parent child: option '--dry' conflicts with "
		  "'--force'\n"
		  "Try 'prog parent child --help' for more information.\n");

	CHECK_INT(PARSE(&f, "parent", "child", "--one", "--two"), ARG_E_RULE);
	CHECK_STR(fx_error(&f)->detail,
		  "options '--one' and '--two' cannot be used together");
	CHECK_INT(PARSE(&f, "parent", "--dry", "child", "--one"), ARG_OK);
}

TEST(root_rules_run_first)
{
	struct fx f;

	fx_init(&f, app);
	/* both the root and the child rule fail: root is reported */
	CHECK_INT(PARSE(&f, "-q", "--verbose", "export", "--json",
			"--yaml"),
		  ARG_E_RULE);
	CHECK_STR(fx_error(&f)->detail,
		  "option '--quiet' conflicts with '--verbose'");
	/* inside a command: declaration order */
	CHECK_INT(PARSE(&f, "export", "--json", "--yaml", "--user", "u"),
		  ARG_E_RULE);
	CHECK_STR(fx_error(&f)->detail,
		  "options '--json' and '--yaml' cannot be used together");
}

TEST(unselected_commands_are_ignored)
{
	struct fx f;

	fx_init(&f, app);
	/* "in" has a ONE_OF that would fail, but it is not selected */
	CHECK_INT(PARSE(&f, "export"), ARG_OK);
	CHECK_INT(PARSE(&f, "parent"), ARG_OK);
}

/* ------------------------------------------------------------------ */
/* Short-only members, ARG_USE                                        */
/* ------------------------------------------------------------------ */

TEST(short_only_members)
{
	struct fx f;

	fx_init(&f, app);
	CHECK_INT(PARSE(&f, "short", "-x"), ARG_OK);
	CHECK_INT(PARSE(&f, "short", "-v", "-y"), ARG_OK);
	CHECK_INT(PARSE(&f, "short", "-xy"), ARG_E_RULE);
	CHECK_STR(report(&f),
		  "prog short: options '-x' and '-y' cannot be used "
		  "together\n"
		  "Try 'prog short --help' for more information.\n");
	/* "-v" names an option that also has a long name */
	CHECK_INT(PARSE(&f, "short", "-vvx"), ARG_E_RULE);
	CHECK_STR(fx_error(&f)->detail,
		  "option '--level' conflicts with '-x'");
}

TEST(members_from_use)
{
	struct fx f;

	fx_init(&f, app);
	CHECK_INT(PARSE(&f, "use", "--color"), ARG_OK);
	CHECK_INT(PARSE(&f, "use", "--mono", "--color"), ARG_E_RULE);
	CHECK_STR(fx_error(&f)->detail,
		  "options '--color' and '--mono' cannot be used together");
}

/* ------------------------------------------------------------------ */
/* What counts as set (SDD 10.2)                                      */
/* ------------------------------------------------------------------ */

static const struct fx_env env_a[] = { { "RULE_A", "from-env" },
				       { NULL, NULL } };

TEST(defaults_do_not_count)
{
	struct fx f;

	fx_init(&f, app);
	/* b has a default; a given on the CLI: fine */
	CHECK_INT(PARSE(&f, "src", "--a", "1"), ARG_OK);
	CHECK_STR(arg_str(&f.p, "b"), "x");
	CHECK_INT(PARSE(&f, "src", "--a", "1", "--b", "2"), ARG_E_RULE);
	CHECK_STR(fx_error(&f)->detail,
		  "options '--a' and '--b' cannot be used together");
}

TEST(env_counts)
{
	struct fx f;

	fx_init(&f, app);
	f.env = env_a;
	CHECK_INT(PARSE(&f, "src"), ARG_OK);
	CHECK_INT(PARSE(&f, "src", "--b", "y"), ARG_E_RULE);
	CHECK_STR(fx_error(&f)->detail,
		  "options '--a' and '--b' cannot be used together");
}

TEST(mask_with_defaults)
{
	struct fx f;

	fx_config(&f, app);
	f.cfg.rule_sources = ARG_RULE_SOURCES_DEFAULT |
			     ARG_SRC_BIT(ARG_SRC_DEFAULT);
	fx_start(&f);
	CHECK_INT(PARSE(&f, "src"), ARG_OK);
	CHECK_INT(PARSE(&f, "src", "--a", "1"), ARG_E_RULE);
	CHECK_STR(fx_error(&f)->detail,
		  "options '--a' and '--b' cannot be used together");
}

TEST(mask_cli_only)
{
	struct fx f;

	fx_config(&f, app);
	f.cfg.rule_sources = ARG_SRC_BIT(ARG_SRC_CLI);
	fx_start(&f);
	f.env = env_a;
	/* a comes from the environment and does not count */
	CHECK_INT(PARSE(&f, "src", "--b", "y"), ARG_OK);
	CHECK_STR(arg_str(&f.p, "a"), "from-env");
	CHECK_INT(PARSE(&f, "src", "--b", "y", "--a", "z"), ARG_E_RULE);
}

/* ------------------------------------------------------------------ */
/* Custom rules                                                       */
/* ------------------------------------------------------------------ */

struct seen {
	const arg_item *rule;
	int calls;
};

static int range_rule(arg *a, const arg_item *rule)
{
	struct seen *s = rule->udata;

	s->rule = rule;
	s->calls++;
	if (arg_int(a, "lo") > arg_int(a, "hi"))
		return arg_fail(a, "--lo must not exceed --hi");
	if (arg_int(a, "lo") == 42)
		return 7; /* plain failure, no message */
	if (arg_int(a, "lo") == 41)
		arg_fail(a, "ignored: the rule passed");
	return 0;
}

static struct seen seen_custom;

static const arg_item custom[] = { ARG_APP(
	"c", "Custom", ARG_INT(0, "lo", "Low", .def = "0"),
	ARG_INT(0, "hi", "High", .def = "100"),
	ARG_RULE_FN(range_rule, &seen_custom)) };

TEST(custom_rule)
{
	struct fx f;

	memset(&seen_custom, 0, sizeof(seen_custom));
	fx_init(&f, custom);
	CHECK_INT(PARSE(&f, "--lo", "5"), ARG_OK);
	CHECK_INT(seen_custom.calls, 1);
	CHECK(seen_custom.rule == &custom[0].items[2]);

	CHECK_INT(PARSE(&f, "--lo", "41"), ARG_OK);
	CHECK(fx_error(&f)->status == ARG_OK);

	CHECK_INT(PARSE(&f, "--lo", "500"), ARG_E_RULE);
	CHECK(fx_error(&f)->item == NULL);
	CHECK_STR(fx_error(&f)->detail, "--lo must not exceed --hi");
	CHECK_STR(report(&f),
		  "prog: --lo must not exceed --hi\n"
		  "Try 'prog --help' for more information.\n");
	CHECK_INT(arg_report(&f.p), 2);
}

TEST(custom_rule_plain_failure)
{
	struct fx f;

	fx_init(&f, custom);
	CHECK_INT(PARSE(&f, "--lo", "42"), ARG_E_RULE);
	CHECK(fx_error(&f)->detail == NULL);
	CHECK_STR(report(&f),
		  "prog: rule violated\n"
		  "Try 'prog --help' for more information.\n");
}

/* ------------------------------------------------------------------ */
/* ARG_DO skips rules                                                 */
/* ------------------------------------------------------------------ */

static const arg_item with_do[] = { ARG_APP(
	"d", "Do", ARG_FLAG(0, "a", "A"), ARG_FLAG(0, "b", "B"),
	ARG_DO(0, "list", do_list, "List"),
	ARG_DO(0, "dump", do_list, "Dump", .check_on_do = 1),
	ARG_ONE_OF("a", "b")) };

TEST(do_skips_rules)
{
	struct fx f;

	fx_init(&f, with_do);
	CHECK_INT(PARSE0(&f), ARG_E_RULE);
	CHECK_INT(PARSE(&f, "--list"), ARG_OK);
	CHECK_INT(arg_run(&f.p), 3);
	CHECK_INT(PARSE(&f, "--list", "--a", "--b"), ARG_OK);
	/* .check_on_do keeps the rules */
	CHECK_INT(PARSE(&f, "--dump"), ARG_E_RULE);
	CHECK_STR(fx_error(&f)->detail,
		  "exactly one of '--a', '--b' is required");
	CHECK_INT(PARSE(&f, "--dump", "--a"), ARG_OK);
}

/* DO and ON options can be rule members, too. */
static const arg_item do_member[] = { ARG_APP(
	"d", "Do", ARG_FLAG(0, "a", "A"),
	ARG_DO('L', "list", do_list, "List", .check_on_do = 1),
	ARG_CONFLICTS("list", "a")) };

TEST(do_as_member)
{
	struct fx f;

	fx_init(&f, do_member);
	CHECK_INT(PARSE(&f, "--a"), ARG_OK);
	CHECK_INT(PARSE(&f, "-L"), ARG_OK);
	CHECK_INT(PARSE(&f, "-L", "--a"), ARG_E_RULE);
	CHECK_STR(fx_error(&f)->detail,
		  "option '--list' conflicts with '--a'");
}

/* ------------------------------------------------------------------ */
/* Spec errors                                                        */
/* ------------------------------------------------------------------ */

static int never(arg *a, const arg_item *rule)
{
	(void)a;
	(void)rule;
	return 0;
}

static void spec_fails(struct test_state *t_, const arg_item *spec,
		       const char *detail, const char *text)
{
	arg_error e;

	CHECK_INT(arg_compile(spec, NULL, &e), ARG_E_SPEC);
	CHECK_INT(e.status, ARG_E_SPEC);
	CHECK_STR(e.detail, detail);
	REQUIRE(e.item != NULL);
	CHECK_INT(e.item->kind, ARG_I_RULE);
	CHECK_INT(e.argi, -1);
	if (text)
		CHECK_STRN(e.text, e.text_len, text);
	else
		CHECK(e.text == NULL);
}

TEST(spec_unknown_name)
{
	const arg_item a[] = { ARG_APP("t", "T", ARG_FLAG(0, "json", ""),
				       ARG_MUTEX("json", "yml")) };
	const arg_item b[] = { ARG_APP("t", "T", ARG_FLAG('x', NULL, ""),
				       ARG_FLAG(0, "y", ""),
				       ARG_REQUIRES("-z", "y")) };
	/* a positional is not an option */
	const arg_item c[] = { ARG_APP("t", "T", ARG_FLAG(0, "y", ""),
				       ARG_ARG("file", ""),
				       ARG_ANY_OF("y", "file")) };
	/* the "no-" name of a negatable flag is not a member name */
	const arg_item d[] = { ARG_APP("t", "T",
				       ARG_FLAG(0, "y", "", .negatable = 1),
				       ARG_FLAG(0, "z", ""),
				       ARG_CONFLICTS("no-y", "z")) };
	const arg_item e[] = { ARG_APP("t", "T", ARG_FLAG(0, "y", ""),
				       ARG_FLAG(0, "z", ""),
				       ARG_CONFLICTS("", "z")) };

	spec_fails(t_, a, "rule names an unknown option", "yml");
	spec_fails(t_, b, "rule names an unknown option", "-z");
	spec_fails(t_, c, "rule names an unknown option", "file");
	spec_fails(t_, d, "rule names an unknown option", "no-y");
	spec_fails(t_, e, "rule names an unknown option", "");
}

TEST(spec_parent_option_is_unknown)
{
	/* only options of the rule's own command are members */
	const arg_item a[] = { ARG_APP(
		"t", "T", ARG_MAIN(nop), ARG_FLAG(0, "all", "", .global = 1),
		ARG_CMD("sub", nop, "", ARG_FLAG(0, "one", ""),
			ARG_MUTEX("all", "one"))) };
	struct fx f;

	spec_fails(t_, a, "rule names an unknown option", "all");
	fx_init(&f, a);
	CHECK_INT(PARSE0(&f), ARG_OK);
	CHECK_INT(PARSE(&f, "sub"), ARG_E_SPEC);
	CHECK_STR(fx_error(&f)->detail, "rule names an unknown option");
}

TEST(spec_too_few_names)
{
	const arg_item a[] = { ARG_APP("t", "T", ARG_FLAG(0, "json", ""),
				       ARG_MUTEX("json")) };
	const arg_item b[] = { ARG_APP("t", "T", ARG_FLAG(0, "json", ""),
				       ARG_ONE_OF("json")) };
	const arg_item c[] = { ARG_APP("t", "T", ARG_FLAG(0, "json", ""),
				       ARG_ANY_OF("json")) };
	const arg_item d[] = { ARG_APP("t", "T", ARG_FLAG(0, "json", ""),
				       ARG_REQUIRES("json")) };
	const arg_item e[] = { ARG_APP("t", "T", ARG_FLAG(0, "json", ""),
				       ARG_CONFLICTS("json")) };
	const arg_item g[] = { ARG_APP(
		"t", "T", ARG_FLAG(0, "json", ""),
		{ .kind = ARG_I_RULE, .rule_kind = ARG_R_MUTEX }) };
	const char *msg = "rule needs at least two options";

	spec_fails(t_, a, msg, NULL);
	spec_fails(t_, b, msg, NULL);
	spec_fails(t_, c, msg, NULL);
	spec_fails(t_, d, msg, NULL);
	spec_fails(t_, e, msg, NULL);
	spec_fails(t_, g, msg, NULL);
}

TEST(spec_duplicate_member)
{
	const arg_item a[] = { ARG_APP("t", "T", ARG_FLAG(0, "json", ""),
				       ARG_FLAG(0, "yaml", ""),
				       ARG_MUTEX("json", "yaml", "json")) };
	/* the same option by long name, alias and short name */
	const arg_item b[] = { ARG_APP(
		"t", "T", ARG_FLAG('j', "json", "", .alias = "js"),
		ARG_FLAG(0, "yaml", ""), ARG_MUTEX("yaml", "js", "-j")) };

	spec_fails(t_, a, "rule names an option twice", "json");
	spec_fails(t_, b, "rule names an option twice", "-j");
}

TEST(spec_custom_without_function)
{
	const arg_item a[] = { ARG_APP("t", "T",
				       ARG_RULE_FN(NULL, NULL)) };
	/* custom rules may list members; they are checked, too */
	const arg_item b[] = { ARG_APP(
		"t", "T", ARG_FLAG(0, "y", ""),
		{ .kind = ARG_I_RULE, .rule_kind = ARG_R_CUSTOM,
		  .fn.rule = never, .names = ARG_CHOICES("y", "q") }) };
	const arg_item c[] = { ARG_APP(
		"t", "T", ARG_FLAG(0, "y", ""),
		{ .kind = ARG_I_RULE, .rule_kind = ARG_R_CUSTOM,
		  .fn.rule = never, .names = ARG_CHOICES("y") }) };

	spec_fails(t_, a, "custom rule without a function", NULL);
	spec_fails(t_, b, "rule names an unknown option", "q");
	CHECK_INT(arg_compile(c, NULL, NULL), ARG_OK);
}

TEST(spec_bad_kind)
{
	const arg_item a[] = { ARG_APP(
		"t", "T", ARG_FLAG(0, "y", ""), ARG_FLAG(0, "z", ""),
		{ .kind = ARG_I_RULE, .rule_kind = ARG_R_NONE,
		  .names = ARG_CHOICES("y", "z") }) };
	const arg_item b[] = { ARG_APP(
		"t", "T", ARG_FLAG(0, "y", ""), ARG_FLAG(0, "z", ""),
		{ .kind = ARG_I_RULE, .rule_kind = (arg_rule_kind)99,
		  .names = ARG_CHOICES("y", "z") }) };

	spec_fails(t_, a, "unknown rule kind", NULL);
	spec_fails(t_, b, "unknown rule kind", NULL);
}

TEST(spec_error_through_parse)
{
	const arg_item a[] = { ARG_APP("t", "T", ARG_FLAG(0, "json", ""),
				       ARG_MUTEX("json", "yml")) };
	struct fx f;

	fx_init(&f, a);
	CHECK_INT(PARSE0(&f), ARG_E_SPEC);
	CHECK_STR(fx_error(&f)->detail, "rule names an unknown option");
	CHECK_INT(fx_error(&f)->item->kind, ARG_I_RULE);
	CHECK(fx_error(&f)->cmd == &a[0]);
}

TEST(spec_valid_tree)
{
	CHECK_INT(arg_compile(app, NULL, NULL), ARG_OK);
	CHECK_INT(arg_compile(with_do, NULL, NULL), ARG_OK);
	CHECK_INT(arg_compile(custom, NULL, NULL), ARG_OK);
}

/* ------------------------------------------------------------------ */
/* Arena exhaustion                                                   */
/* ------------------------------------------------------------------ */

union big_arena {
	unsigned char b[16384];
	long double align_ld;
	void *align_p;
	uint64_t align_u;
};

/*
 * Grow a caller arena until the parse gets past index building; at
 * the smallest such size the message cannot be built and the static
 * fallback text is used.
 */
static arg_status tight_parse(struct fx *f, union big_arena *buf,
			      char *const *argv)
{
	size_t cap;
	arg_status st = ARG_E_NO_MEMORY;

	for (cap = 16; cap <= sizeof(buf->b) && st == ARG_E_NO_MEMORY;
	     cap += 8) {
		fx_config(f, app);
		f->cfg.arena = buf->b;
		f->cfg.arena_cap = cap;
		fx_start(f);
		st = fx_parse(f, argv);
	}
	return st;
}

TEST(arena_exhaustion_fallback)
{
	static union big_arena buf;
	static char *const mutex[] = { "prog", "export", "--json", "--yaml",
				       NULL };
	static char *const one_of[] = { "prog", "in", NULL };
	static char *const several[] = { "prog", "in", "--net", "--stdin",
					 NULL };
	static char *const any_of[] = { "prog", "edit", NULL };
	static char *const req[] = { "prog", "export", "--user", "u", NULL };
	static char *const conf[] = { "prog", "-q", "--verbose", NULL };
	struct fx f;

	CHECK_INT(tight_parse(&f, &buf, mutex), ARG_E_RULE);
	CHECK_STR(fx_error(&f)->detail, "options cannot be used together");
	CHECK_STR(fx_error(&f)->item->name, "yaml");
	CHECK_STR(report(&f),
		  "prog export: options cannot be used together\n"
		  "Try 'prog export --help' for more information.\n");

	CHECK_INT(tight_parse(&f, &buf, one_of), ARG_E_RULE);
	CHECK_STR(fx_error(&f)->detail,
		  "exactly one of the options is required");
	CHECK_INT(tight_parse(&f, &buf, several), ARG_E_RULE);
	CHECK_STR(fx_error(&f)->detail, "options cannot be used together");
	CHECK_INT(tight_parse(&f, &buf, any_of), ARG_E_RULE);
	CHECK_STR(fx_error(&f)->detail,
		  "at least one of the options is required");
	CHECK_INT(tight_parse(&f, &buf, req), ARG_E_RULE);
	CHECK_STR(fx_error(&f)->detail, "option requires another option");
	CHECK_INT(tight_parse(&f, &buf, conf), ARG_E_RULE);
	CHECK_STR(fx_error(&f)->detail,
		  "option conflicts with another option");
}

TEST_MAIN(TEST_CASE(mutex_ok), TEST_CASE(mutex_violation),
	  TEST_CASE(one_of_ok), TEST_CASE(one_of_none),
	  TEST_CASE(one_of_several), TEST_CASE(any_of),
	  TEST_CASE(requires_ok), TEST_CASE(requires_violation),
	  TEST_CASE(conflicts), TEST_CASE(parent_rules_apply_to_children),
	  TEST_CASE(root_rules_run_first),
	  TEST_CASE(unselected_commands_are_ignored),
	  TEST_CASE(short_only_members), TEST_CASE(members_from_use),
	  TEST_CASE(defaults_do_not_count), TEST_CASE(env_counts),
	  TEST_CASE(mask_with_defaults), TEST_CASE(mask_cli_only),
	  TEST_CASE(custom_rule), TEST_CASE(custom_rule_plain_failure),
	  TEST_CASE(do_skips_rules), TEST_CASE(do_as_member),
	  TEST_CASE(spec_unknown_name),
	  TEST_CASE(spec_parent_option_is_unknown),
	  TEST_CASE(spec_too_few_names), TEST_CASE(spec_duplicate_member),
	  TEST_CASE(spec_custom_without_function), TEST_CASE(spec_bad_kind),
	  TEST_CASE(spec_error_through_parse), TEST_CASE(spec_valid_tree),
	  TEST_CASE(arena_exhaustion_fallback))
