// SPDX-License-Identifier: GPL-2.0-only
/*
 * test_parse_cmds.c - subcommands, globals, dispatch, ARG_USE (SDD 6.6, 9)
 */
#include "parse_util.h"

/* Handlers record what ran in the config's udata. */
struct rec {
	const char *ran;
	int argc;
	char *const *argv;
	const char *path;
};

static int record(arg *a, const char *what)
{
	struct rec *r = arg_udata(a);

	r->ran = what;
	r->path = arg_path(a);
	return 0;
}

static int install(arg *a)
{
	return record(a, "install");
}

static int repo_add(arg *a)
{
	return record(a, "repo add");
}

static int repo_list(arg *a)
{
	record(a, "repo list");
	return 7;
}

static int key_rm(arg *a)
{
	return record(a, "repo key rm");
}

static int root_main(arg *a)
{
	return record(a, "main");
}

static int external(arg *a, int argc, char *const *argv)
{
	struct rec *r = arg_udata(a);

	r->ran = "external";
	r->argc = argc;
	r->argv = argv;
	return 42;
}

/* The repo family lives in its own "file" (SDD 6.6). */
ARG_DEFINE(repo_tree,
	   ARG_CMD("repo", NULL, "Manage repositories",
		   ARG_FLAG('q', "quiet", "Less output"),
		   ARG_CMD("add", repo_add, "Add a repository",
			   ARG_ARG("name", "Repository name"),
			   ARG_ARG("url", "Repository URL"),
			   ARG_FLAG(0, "trusted", "Skip checks")),
		   ARG_CMD("list", repo_list, "List repositories",
			   ARG_ALIAS("ls")),
		   ARG_CMD("key", NULL, "Keys",
			   ARG_CMD("rm", key_rm, "Remove a key",
				   ARG_ALIAS("remove"),
				   ARG_ARG("id", "Key id")))));
ARG_EXTERN(repo_tree);

static const arg_item pkg[] = { ARG_APP(
	"pkg", "A tiny package manager.",
	ARG_FLAG('v', "verbose", "Print more", .global = 1),
	ARG_STR('r', "root", "Install prefix", .def = "/"),
	ARG_CMD("install", install, "Install packages", ARG_ALIAS("i"),
		ARG_FLAG('f', "force", "Reinstall"),
		ARG_ARGS("name", "Packages")),
	ARG_USE(repo_tree)) };

static void fx_rec(struct fx *f, const arg_item *spec, struct rec *r)
{
	memset(r, 0, sizeof(*r));
	fx_config(f, spec);
	f->cfg.udata = r;
}

TEST(nested_three_deep)
{
	struct fx f;
	struct rec r;

	fx_rec(&f, pkg, &r);
	fx_start(&f);
	REQUIRE(PARSE(&f, "repo", "key", "rm", "abc") == ARG_OK);
	CHECK(arg_path_is(&f.p, "repo key rm"));
	CHECK(arg_path_is(&f.p, " repo  key rm "));
	CHECK(!arg_path_is(&f.p, "repo key"));
	CHECK(!arg_path_is(&f.p, "repo key rm x"));
	CHECK(!arg_path_is(&f.p, "repo kex rm"));
	CHECK(!arg_path_is(&f.p, ""));
	CHECK(!arg_path_is(&f.p, NULL));
	CHECK_STR(arg_path(&f.p), "prog repo key rm");
	CHECK_STR(arg_str(&f.p, "id"), "abc");
	CHECK_INT(arg_run(&f.p), 0);
	CHECK_STR(r.ran, "repo key rm");
	CHECK_STR(r.path, "prog repo key rm");

	/* aliases select the canonical command */
	REQUIRE(PARSE(&f, "repo", "key", "remove", "abc") == ARG_OK);
	CHECK(arg_path_is(&f.p, "repo key rm"));
	REQUIRE(PARSE(&f, "repo", "ls") == ARG_OK);
	CHECK(arg_path_is(&f.p, "repo list"));
	CHECK_INT(arg_run(&f.p), 7);
	CHECK_STR(r.ran, "repo list");
	REQUIRE(PARSE(&f, "i", "a", "b") == ARG_OK);
	CHECK(arg_path_is(&f.p, "install"));
	CHECK_UINT(arg_count(&f.p, "name"), 2);
	/* commands are keys too */
	CHECK(arg_flag(&f.p, "install"));
	CHECK(arg_is_set(&f.p, "install"));
	CHECK_UINT(arg_count(&f.p, "install"), 1);
}

TEST(globals_before_and_after)
{
	struct fx f;
	struct rec r;

	fx_rec(&f, pkg, &r);
	fx_start(&f);
	REQUIRE(PARSE(&f, "-v", "repo", "add", "main", "url") == ARG_OK);
	CHECK(arg_flag(&f.p, "verbose"));
	REQUIRE(PARSE(&f, "repo", "add", "main", "url", "-v") == ARG_OK);
	CHECK(arg_flag(&f.p, "verbose"));
	REQUIRE(PARSE(&f, "repo", "-v", "add", "--verbose", "x", "y") ==
		ARG_OK);
	CHECK(arg_flag(&f.p, "verbose"));
	CHECK_STR(arg_str(&f.p, "name"), "x");
	CHECK_STR(arg_str(&f.p, "url"), "y");
	/* non-global parent options are not valid below */
	CHECK_INT(PARSE(&f, "repo", "--root=/x", "list"),
		  ARG_E_UNKNOWN_OPTION);
	CHECK_STR(fx_report(&f),
		  "prog repo: unknown option '--root'\n"
		  "Try 'prog repo --help' for more information.\n");
	CHECK_INT(PARSE(&f, "repo", "add", "-q", "a", "b"),
		  ARG_E_UNKNOWN_OPTION);
	/* ... but handlers still read them (ancestor lookup) */
	REQUIRE(PARSE(&f, "-r", "/opt", "repo", "-q", "add", "a", "b") ==
		ARG_OK);
	CHECK_STR(arg_str(&f.p, "root"), "/opt");
	CHECK(arg_flag(&f.p, "quiet"));
	CHECK(!arg_flag(&f.p, "trusted"));
	/* suggestions include reachable globals */
	CHECK_INT(PARSE(&f, "repo", "add", "--verbos"), ARG_E_UNKNOWN_OPTION);
	CHECK_STR(fx_error(&f)->suggestion, "verbose");
	CHECK_INT(PARSE(&f, "repo", "add", "--rot"), ARG_E_UNKNOWN_OPTION);
	CHECK(fx_error(&f)->suggestion == NULL);
}

TEST(grouping_command)
{
	struct fx f;
	struct rec r;

	fx_rec(&f, pkg, &r);
	fx_start(&f);
	CHECK_INT(PARSE(&f, "repo"), ARG_E_MISSING_COMMAND);
	CHECK(fx_error(&f)->cmd == &repo_tree[0]);
	CHECK_STR(fx_report(&f),
		  "prog repo: missing command\n"
		  "Try 'prog repo --help' for more information.\n");
	CHECK_INT(arg_run(&f.p), 2);
	CHECK(r.ran == NULL);
	CHECK_INT(PARSE(&f, "repo", "key"), ARG_E_MISSING_COMMAND);
	CHECK_INT(PARSE0(&f), ARG_E_MISSING_COMMAND);
	CHECK_INT(PARSE(&f, "repo", "--help"), ARG_EXIT_HELP);
	CHECK_INT(arg_run(&f.p), 0);
	CHECK(r.ran == NULL);
}

TEST(unknown_command)
{
	struct fx f;
	struct rec r;
	const arg_error *e;

	fx_rec(&f, pkg, &r);
	fx_start(&f);
	CHECK_INT(PARSE(&f, "instal", "x"), ARG_E_UNKNOWN_COMMAND);
	e = fx_error(&f);
	CHECK_STR(e->suggestion, "install");
	CHECK_STR(e->suggestion_prefix, "");
	CHECK_INT(e->argi, 1);
	CHECK_STR(fx_report(&f), "prog: unknown command 'instal'\n"
				 "  Did you mean 'install'?\n"
				 "Try 'prog --help' for more information.\n");
	CHECK_INT(PARSE(&f, "repo", "lst"), ARG_E_UNKNOWN_COMMAND);
	CHECK_STR(fx_error(&f)->suggestion, "list");
	CHECK_INT(PARSE(&f, "repo", "zzzzzz"), ARG_E_UNKNOWN_COMMAND);
	CHECK(fx_error(&f)->suggestion == NULL);
	/* leaf commands take positionals, too many is an error */
	CHECK_INT(PARSE(&f, "repo", "add", "a", "b", "c"),
		  ARG_E_TOO_MANY_ARGS);
	CHECK_STRN(fx_error(&f)->text, fx_error(&f)->text_len, "c");
	CHECK_STR(fx_report(&f),
		  "prog repo add: unexpected argument 'c'\n"
		  "Try 'prog repo add --help' for more information.\n");
}

TEST(external_commands)
{
	char *argv[] = { "pkg", "-v", "audit", "--deep", "x", NULL };
	struct fx f;
	struct rec r;

	fx_rec(&f, pkg, &r);
	f.cfg.on_unknown_cmd = external;
	fx_start(&f);
	REQUIRE(fx_parse(&f, argv) == ARG_OK);
	CHECK(arg_flag(&f.p, "verbose"));
	CHECK_STR(arg_str(&f.p, "root"), "/");
	CHECK_INT(arg_run(&f.p), 42);
	CHECK_STR(r.ran, "external");
	CHECK_INT(r.argc, 3);
	CHECK(r.argv == argv + 2);
	/* known names still win */
	REQUIRE(PARSE(&f, "repo", "list") == ARG_OK);
	CHECK_INT(arg_run(&f.p), 7);
	/* grouping checks are skipped for claimed commands */
	REQUIRE(PARSE(&f, "repo", "bogus") == ARG_OK);
	CHECK_INT(arg_run(&f.p), 42);
	CHECK_INT(r.argc, 1);
}

TEST(positionals_versus_commands)
{
	const arg_item spec[] = { ARG_APP(
		"t", "T", ARG_ARG_OPT("file", "File"),
		ARG_CMD("run", root_main, "Run", ARG_ARGS_OPT("x", "X")),
		ARG_MAIN(root_main)) };
	struct fx f;
	struct rec r;

	fx_rec(&f, spec, &r);
	fx_start(&f);
	REQUIRE(PARSE(&f, "run", "run") == ARG_OK);
	CHECK(arg_path_is(&f.p, "run"));
	CHECK_STR(arg_str(&f.p, "x"), "run");
	/* once a positional has a value, a command name is just a value */
	CHECK_INT(PARSE(&f, "a", "run"), ARG_E_TOO_MANY_ARGS);
	REQUIRE(PARSE(&f, "a") == ARG_OK);
	CHECK(arg_path_is(&f.p, ""));
	CHECK_STR(arg_str(&f.p, "file"), "a");
	CHECK_INT(arg_run(&f.p), 0);
	CHECK_STR(r.ran, "main");
	/* with positionals an unknown word is a positional, not a command */
	REQUIRE(PARSE(&f, "rnu") == ARG_OK);
	CHECK_STR(arg_str(&f.p, "file"), "rnu");
	/* the root handler runs without a subcommand: no missing command */
	REQUIRE(PARSE0(&f) == ARG_OK);
	CHECK_STR(arg_path(&f.p), "prog");
}

TEST(posix_with_subcommands)
{
	const arg_item spec[] = { ARG_APP(
		"t", "T", ARG_FLAG('v', "verbose", "V", .global = 1),
		ARG_CMD("run", root_main, "Run", ARG_FLAG('f', "fast", "F"),
			ARG_ARGS_OPT("x", "X"))) };
	struct fx f;
	struct rec r;

	fx_rec(&f, spec, &r);
	f.cfg.style = ARG_STYLE_POSIX;
	fx_start(&f);
	REQUIRE(PARSE(&f, "-v", "run", "-f", "a", "-v") == ARG_OK);
	CHECK(arg_flag(&f.p, "fast"));
	CHECK_UINT(arg_count(&f.p, "verbose"), 1);
	CHECK_UINT(arg_count(&f.p, "x"), 2);
	CHECK_STR(arg_str_at(&f.p, "x", 1), "-v");
}

TEST(help_command)
{
	struct fx f;
	struct rec r;

	fx_rec(&f, pkg, &r);
	fx_start(&f);
	/* without the flag "help" is just an unknown command */
	CHECK_INT(PARSE(&f, "help"), ARG_E_UNKNOWN_COMMAND);

	fx_rec(&f, pkg, &r);
	f.cfg.style = ARG_HELP_COMMAND;
	fx_start(&f);
	CHECK_INT(PARSE(&f, "help"), ARG_EXIT_HELP);
	CHECK_STR(arg_path(&f.p), "prog");
	CHECK_INT(PARSE(&f, "help", "repo", "ls"), ARG_EXIT_HELP);
	CHECK(arg_path_is(&f.p, "repo list"));
	test_buf_reset(&f.out);
	CHECK_INT(arg_report(&f.p), 0);
	CHECK_STR(f.out.data, "Usage: prog repo list [OPTIONS]\n");
	CHECK_INT(PARSE(&f, "repo", "help", "add"), ARG_EXIT_HELP);
	CHECK(arg_path_is(&f.p, "repo add"));
	CHECK_INT(PARSE(&f, "help", "repo", "-x"), ARG_EXIT_HELP);
	CHECK(arg_path_is(&f.p, "repo"));
	CHECK_INT(PARSE(&f, "help", "nope"), ARG_E_UNKNOWN_COMMAND);
	CHECK_INT(PARSE(&f, "hlp"), ARG_E_UNKNOWN_COMMAND);
	CHECK_STR(fx_error(&f)->suggestion, "help");
	/* a leaf has no subcommands: "help" is a positional there */
	REQUIRE(PARSE(&f, "install", "help") == ARG_OK);
	CHECK_STR(arg_str(&f.p, "name"), "help");
}

TEST(help_render_path)
{
	struct fx f;
	struct rec r;

	fx_rec(&f, pkg, &r);
	fx_start(&f);
	CHECK_INT(PARSE(&f, "repo", "add", "-h"), ARG_EXIT_HELP);
	test_buf_reset(&f.out);
	CHECK_INT(arg_report(&f.p), 0);
	CHECK_STR(f.out.data, "Usage: prog repo add [OPTIONS]\n");
	/* arg_help on any path, and the selected one */
	test_buf_reset(&f.out);
	CHECK_INT(arg_help(&f.p, "repo key rm", NULL, NULL), 0);
	CHECK_STR(f.out.data, "Usage: prog repo key rm [OPTIONS]\n");
	test_buf_reset(&f.out);
	CHECK_INT(arg_help(&f.p, NULL, fx_out, &f), 0);
	CHECK_STR(f.out.data, "Usage: prog repo add [OPTIONS]\n");
	test_buf_reset(&f.out);
	CHECK_INT(arg_help(&f.p, "", fx_out, &f), 0);
	CHECK_STR(f.out.data, "Usage: prog [OPTIONS]\n");
	CHECK_INT(arg_help(&f.p, "repo nope", fx_out, &f), -1);
	CHECK_INT(arg_completion(&f.p, ARG_SH_BASH, fx_out, &f), -1);
}

static int custom_help(arg *a, const arg_item *cmd, arg_write_fn w, void *ud)
{
	(void)a;
	return w(ud, cmd->name, strlen(cmd->name));
}

TEST(help_hook)
{
	struct fx f;
	struct rec r;

	fx_rec(&f, pkg, &r);
	f.cfg.render_help = custom_help;
	fx_start(&f);
	CHECK_INT(PARSE(&f, "install", "--help"), ARG_EXIT_HELP);
	test_buf_reset(&f.out);
	CHECK_INT(arg_report(&f.p), 0);
	CHECK_STR(f.out.data, "install");
	test_buf_reset(&f.out);
	CHECK_INT(arg_help(&f.p, "repo", NULL, NULL), 0);
	CHECK_STR(f.out.data, "repo");
}

TEST(errors_carry_the_path)
{
	struct fx f;
	struct rec r;

	fx_rec(&f, pkg, &r);
	fx_start(&f);
	CHECK_INT(PARSE(&f, "repo", "add", "main"), ARG_E_TOO_FEW_ARGS);
	CHECK(fx_error(&f)->item == &repo_tree[0].items[1].items[1]);
	CHECK_STR(fx_report(&f),
		  "prog repo add: missing required argument <url>\n"
		  "Try 'prog repo add --help' for more information.\n");
	CHECK_INT(PARSE(&f, "install"), ARG_E_TOO_FEW_ARGS);
}

TEST(use_splicing)
{
	ARG_DEFINE(opts, ARG_FLAG('a', "all", "All"),
		   ARG_USE(((const arg_item[]){ ARG_FLAG('b', "big", "Big"),
						ARG__END })));
	const arg_item spec[] = { ARG_APP("t", "T", ARG_USE(opts),
					  ARG_INT('n', "num", "N")) };
	struct fx f;

	fx_init(&f, spec);
	REQUIRE(PARSE(&f, "-ab", "--num=3", "--big") == ARG_OK);
	CHECK(arg_flag(&f.p, "all"));
	CHECK(arg_flag(&f.p, "big"));
	CHECK_INT(arg_int(&f.p, "num"), 3);
}

TEST(parse_twice_resets)
{
	struct fx f;
	struct rec r;

	fx_rec(&f, pkg, &r);
	fx_start(&f);
	REQUIRE(PARSE(&f, "-v", "repo", "add", "a", "b") == ARG_OK);
	REQUIRE(PARSE(&f, "install", "x") == ARG_OK);
	CHECK(!arg_flag(&f.p, "verbose"));
	CHECK(arg_path_is(&f.p, "install"));
	CHECK_INT(arg_run(&f.p), 0);
	CHECK_STR(r.ran, "install");
}

TEST_MAIN(TEST_CASE(nested_three_deep), TEST_CASE(globals_before_and_after),
	  TEST_CASE(grouping_command), TEST_CASE(unknown_command),
	  TEST_CASE(external_commands),
	  TEST_CASE(positionals_versus_commands),
	  TEST_CASE(posix_with_subcommands), TEST_CASE(help_command),
	  TEST_CASE(help_render_path), TEST_CASE(help_hook),
	  TEST_CASE(errors_carry_the_path), TEST_CASE(use_splicing),
	  TEST_CASE(parse_twice_resets))
