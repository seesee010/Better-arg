// SPDX-License-Identifier: GPL-2.0-only
/*
 * test_parse_actions.c - ARG_DO, ARG_ON, arg_run(), arg_report(),
 * arg_fail() and the exit-on-error path (SDD 6.5, 6.7, 10.3)
 */
#include "parse_util.h"

#include <setjmp.h>

struct log {
	char text[256];
	int jobs_seen;
	jmp_buf jb;
	int exit_code;
};

static void log_add(arg *a, const char *s)
{
	struct log *l = arg_udata(a);
	size_t n = strlen(l->text);

	snprintf(l->text + n, sizeof(l->text) - n, "%s;", s);
}

static int do_list(arg *a)
{
	struct log *l = arg_udata(a);

	log_add(a, "list");
	l->jobs_seen = (int)arg_int(a, "jobs");
	return 3;
}

static int do_dump(arg *a)
{
	log_add(a, "dump");
	return 4;
}

static int on_include(arg *a, const char *dir)
{
	struct log *l = arg_udata(a);

	if (strcmp(dir, "bad") == 0)
		return arg_fail(a, "not a directory");
	if (strcmp(dir, "worse") == 0)
		return 9;
	l->jobs_seen = (int)arg_int(a, "jobs");
	log_add(a, dir);
	return 0;
}

static int on_quiet(arg *a, const char *v)
{
	log_add(a, v ? v : "(null)");
	return 0;
}

static int run_main(arg *a)
{
	log_add(a, "main");
	if (arg_flag(a, "fail"))
		return arg_fail(a, "it broke");
	if (arg_flag(a, "silent-fail"))
		return 5;
	return 0;
}

static const arg_item app[] = { ARG_APP(
	"t", "T", ARG_INT('j', "jobs", "Jobs", .def = "1"),
	ARG_STR(0, "need", "Needed", .required = 1),
	ARG_DO(0, "list", do_list, "List"),
	ARG_DO(0, "dump", do_dump, "Dump", .check_on_do = 1),
	ARG_ON('I', "include", on_include, "Include", .value = "DIR"),
	ARG_ON('q', "quiet", on_quiet, "Quiet"),
	ARG_ON(0, "level", on_quiet, "Level", .optional_value = 1),
	ARG_FLAG(0, "fail", "Fail"), ARG_FLAG(0, "silent-fail", "Fail"),
	ARG_MAIN(run_main)) };

static void fx_log(struct fx *f, struct log *l)
{
	memset(l, 0, sizeof(*l));
	fx_config(f, app);
	f->cfg.udata = l;
	fx_start(f);
}

TEST(do_flags)
{
	struct fx f;
	struct log l;

	fx_log(&f, &l);
	/* required checks are skipped, values are still stored */
	REQUIRE(PARSE(&f, "--list", "-j", "5") == ARG_OK);
	CHECK(arg_flag(&f.p, "list"));
	CHECK_INT(arg_run(&f.p), 3);
	CHECK_STR(l.text, "list;");
	CHECK_INT(l.jobs_seen, 5);
	/* the first DO wins */
	l.text[0] = '\0';
	REQUIRE(PARSE(&f, "--list", "--dump", "--list") == ARG_OK);
	CHECK_UINT(arg_count(&f.p, "list"), 2);
	CHECK_INT(arg_run(&f.p), 3);
	CHECK_STR(l.text, "list;");
	/* .check_on_do keeps the checks */
	CHECK_INT(PARSE(&f, "--dump"), ARG_E_MISSING_REQUIRED);
	REQUIRE(PARSE(&f, "--dump", "--need=x") == ARG_OK);
	l.text[0] = '\0';
	CHECK_INT(arg_run(&f.p), 4);
	CHECK_STR(l.text, "dump;");
	/* DO flags take no value */
	CHECK_INT(PARSE(&f, "--list=1"), ARG_E_UNEXPECTED_VALUE);
	/* parse errors after a DO still count */
	CHECK_INT(PARSE(&f, "--list", "--bogus"), ARG_E_UNKNOWN_OPTION);
}

TEST(on_flags)
{
	struct fx f;
	struct log l;

	fx_log(&f, &l);
	REQUIRE(PARSE(&f, "-I", "a", "-j3", "--include=b", "-qq", "-Ic",
		      "--need", "x") == ARG_OK);
	CHECK_STR(l.text, "a;b;(null);(null);c;");
	CHECK_INT(l.jobs_seen, 3); /* values given earlier are visible */
	CHECK_UINT(arg_count(&f.p, "include"), 3);
	CHECK_UINT(arg_count(&f.p, "quiet"), 2);
	CHECK(arg_flag(&f.p, "quiet"));
	CHECK(arg_is_set(&f.p, "quiet"));
	l.text[0] = '\0';
	REQUIRE(PARSE(&f, "--level", "--level=3", "--need=x") == ARG_OK);
	CHECK_STR(l.text, "(null);3;");
	CHECK_INT(PARSE(&f, "--quiet=x"), ARG_E_UNEXPECTED_VALUE);
	CHECK_INT(PARSE(&f, "-I"), ARG_E_MISSING_VALUE);
}

TEST(on_failure)
{
	struct fx f;
	struct log l;
	const arg_error *e;

	fx_log(&f, &l);
	CHECK_INT(PARSE(&f, "-I", "ok", "-I", "bad", "-I", "never"),
		  ARG_E_ACTION);
	CHECK_STR(l.text, "ok;");
	e = fx_error(&f);
	CHECK_STR(e->detail, "not a directory");
	CHECK(e->item == &app[0].items[4]);
	CHECK_INT(e->argi, 3);
	CHECK_STRN(e->text, e->text_len, "bad");
	CHECK_STR(fx_report(&f), "prog: not a directory\n");
	CHECK_INT(arg_exit_code(PARSE(&f, "-Iworse")), 1);
	CHECK(fx_error(&f)->detail == NULL);
	CHECK_STR(fx_report(&f), "prog: action failed\n");
	CHECK_INT(arg_run(&f.p), 1);
}

TEST(run_and_fail)
{
	struct fx f;
	struct log l;

	fx_log(&f, &l);
	CHECK_INT(arg_run(&f.p), 0); /* before any parse */
	REQUIRE(PARSE(&f, "--need=x") == ARG_OK);
	CHECK_INT(arg_run(&f.p), 0);
	CHECK_STR(l.text, "main;");
	REQUIRE(PARSE(&f, "--need=x", "--fail") == ARG_OK);
	test_buf_reset(&f.err);
	CHECK_INT(arg_run(&f.p), 1);
	CHECK_STR(f.err.data, "prog: it broke\n");
	REQUIRE(PARSE(&f, "--need=x", "--silent-fail") == ARG_OK);
	test_buf_reset(&f.err);
	CHECK_INT(arg_run(&f.p), 5);
	CHECK_STR(f.err.data, "");
	/* a failed parse runs nothing */
	l.text[0] = '\0';
	CHECK_INT(PARSE0(&f), ARG_E_MISSING_REQUIRED);
	CHECK_INT(arg_run(&f.p), 2);
	CHECK_STR(l.text, "");
	/* arg_fail outside a handler: returns 1, message dropped later */
	CHECK_INT(arg_fail(&f.p, "x"), 1);
}

TEST(report)
{
	struct fx f;
	struct log l;

	fx_log(&f, &l);
	REQUIRE(PARSE(&f, "--need=x") == ARG_OK);
	CHECK_INT(arg_report(&f.p), 0);
	CHECK_INT(arg_error_get(&f.p)->status, ARG_OK);
	CHECK_INT(PARSE(&f, "-h"), ARG_EXIT_HELP);
	test_buf_reset(&f.out);
	CHECK_INT(arg_report(&f.p), 0);
	CHECK_STR(f.out.data, "Usage: prog [OPTIONS]\n");
	CHECK_INT(PARSE(&f, "-x"), ARG_E_UNKNOWN_OPTION);
	test_buf_reset(&f.err);
	CHECK_INT(arg_report(&f.p), 2);
	CHECK_STR(f.err.data, "prog: unknown option '-x'\n"
			      "Try 'prog --help' for more information.\n");
}

static int my_error(arg *a, const arg_error *e)
{
	log_add(a, arg_status_name(e->status));
	return 64;
}

TEST(report_hook)
{
	struct fx f;
	struct log l;

	fx_log(&f, &l);
	f.cfg.on_error = my_error;
	fx_start(&f);
	CHECK_INT(PARSE(&f, "-x"), ARG_E_UNKNOWN_OPTION);
	test_buf_reset(&f.err);
	CHECK_INT(arg_report(&f.p), 64);
	CHECK_STR(f.err.data, "");
	CHECK_STR(l.text, "ARG_E_UNKNOWN_OPTION;");
}

static void jump_exit(void *ud, int code)
{
	struct log *l = ud;

	l->exit_code = code;
	longjmp(l->jb, 1);
}

static void no_exit(void *ud, int code)
{
	struct log *l = ud;

	l->exit_code = code + 100;
}

TEST(exit_on_error)
{
	struct fx f;
	struct log l;
	volatile int reached = 0;

	fx_config(&f, app);
	memset(&l, 0, sizeof(l));
	f.cfg.udata = &l;
	f.cfg.exit = jump_exit;
	f.cfg.exit_ud = &l;
	arg__init(&f.p, app, &f.cfg, 1);
	if (setjmp(l.jb) == 0) {
		PARSE(&f, "--bogus");
		reached = 1;
	}
	CHECK(!reached);
	CHECK_INT(l.exit_code, 2);
	CHECK(strstr(f.err.data, "unknown option '--bogus'") != NULL);
	if (setjmp(l.jb) == 0) {
		PARSE(&f, "--help");
		reached = 1;
	}
	CHECK(!reached);
	CHECK_INT(l.exit_code, 0);
	CHECK_STR(f.out.data, "Usage: prog [OPTIONS]\n");
	/* success does not exit */
	if (setjmp(l.jb) == 0) {
		CHECK_INT(PARSE(&f, "--need=x"), ARG_OK);
		reached = 1;
	}
	CHECK(reached);
	/* an exit hook that returns: the status comes back */
	f.cfg.exit = no_exit;
	arg__init(&f.p, app, &f.cfg, 1);
	CHECK_INT(PARSE0(&f), ARG_E_MISSING_REQUIRED);
	CHECK_INT(l.exit_code, 102);
}

TEST_MAIN(TEST_CASE(do_flags), TEST_CASE(on_flags), TEST_CASE(on_failure),
	  TEST_CASE(run_and_fail), TEST_CASE(report), TEST_CASE(report_hook),
	  TEST_CASE(exit_on_error))
