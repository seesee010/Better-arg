// SPDX-License-Identifier: GPL-2.0-only
/*
 * test_run.c - Level 0: ARG_RUN(), ARG_RUN_EX() and arg__main()
 * (SDD 6.1, 10.3)
 *
 * The file is built with ARG_EXIT_ON_ERROR 0 like every unit test;
 * the exit-on-error path calls arg__main(1, ...) directly with an
 * exit hook that longjmps back.
 */
#include "arg.h"
#include "test.h"

#include <setjmp.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Fixtures                                                           */
/* ------------------------------------------------------------------ */

/* Counts live cfg.alloc blocks, to prove that arg_free() ran. */
struct counter {
	long live;
	long calls;
};

static void *count_alloc(void *ud, void *ptr, size_t old_size,
			 size_t new_size)
{
	struct counter *c = ud;

	(void)old_size;
	c->calls++;
	if (new_size == 0) {
		if (ptr)
			c->live--;
		free(ptr);
		return NULL;
	}
	if (!ptr)
		c->live++;
	return realloc(ptr, new_size);
}

struct env {
	struct test_buf out, err;
	struct counter cnt;
	jmp_buf jb;
	int exit_code;
	int exit_calls;
	long live_at_exit;
	int ran;
	size_t nargs;
};

static int env_out(void *ud, const char *s, size_t n)
{
	return test_buf_write(&((struct env *)ud)->out, s, n);
}

static int env_err(void *ud, const char *s, size_t n)
{
	return test_buf_write(&((struct env *)ud)->err, s, n);
}

static void jump_exit(void *ud, int code)
{
	struct env *e = ud;

	e->exit_code = code;
	e->exit_calls++;
	e->live_at_exit = e->cnt.live;
	longjmp(e->jb, 1);
}

static void soft_exit(void *ud, int code)
{
	struct env *e = ud;

	e->exit_code = code;
	e->exit_calls++;
}

/* Capturing config; e must stay alive while cfg is used. */
static arg_config env_config(struct env *e)
{
	arg_config c = ARG_CONFIG_INIT;

	memset(e, 0, sizeof(*e));
	test_buf_reset(&e->out);
	test_buf_reset(&e->err);
	c.write_out = env_out;
	c.write_err = env_err;
	c.io_ud = e;
	c.udata = e;
	c.exit = soft_exit;
	c.exit_ud = e;
	return c;
}

static int argc_of(char *const *argv)
{
	int n = 0;

	while (argv[n])
		n++;
	return n;
}

#define ARGV(...) ((char *[]){ "prog", __VA_ARGS__, NULL })
#define ARGV0 ((char *[]){ "prog", NULL })

/* ------------------------------------------------------------------ */
/* Handlers                                                           */
/* ------------------------------------------------------------------ */

static int ret_seven(arg *a)
{
	struct env *e = arg_udata(a);

	if (e)
		e->ran++;
	return 7;
}

static int ret_jobs(arg *a)
{
	return (int)arg_int(a, "jobs");
}

static int count_files(arg *a)
{
	struct env *e = arg_udata(a);

	e->ran++;
	e->nargs = arg_count(a, "file");
	return 0;
}

static int do_five(arg *a)
{
	(void)a;
	return 5;
}

static int on_bad(arg *a, const char *v)
{
	(void)v;
	return arg_fail(a, "refused");
}

static int fail_handler(arg *a)
{
	return arg_fail(a, "handler failed") + 3;
}

/* ------------------------------------------------------------------ */
/* Exit codes                                                         */
/* ------------------------------------------------------------------ */

static int run_jobs(arg_config *cfg, char *const *argv)
{
	return ARG_RUN_EX(cfg, argc_of(argv), argv, "prog", "Test.",
		ARG_INT('j', "jobs", "Jobs", .def = "4", .min = "0",
			.max = "99"),
		ARG_DO(0, "five", do_five, "Exit with 5"),
		ARG_ON('X', "bad", on_bad, "Always fails"),
		ARG_MAIN(ret_jobs));
}

TEST(handler_return_is_exit_code)
{
	struct env e;
	arg_config c = env_config(&e);

	CHECK_INT(run_jobs(&c, ARGV0), 4);
	CHECK_INT(run_jobs(&c, ARGV("-j", "42")), 42);
	CHECK_INT(run_jobs(&c, ARGV("--jobs=0")), 0);
	CHECK_STR(e.err.data, "");
	CHECK_INT(e.exit_calls, 0);
}

TEST(usage_errors_exit_2)
{
	struct env e;
	arg_config c = env_config(&e);

	CHECK_INT(run_jobs(&c, ARGV("-j", "100")), 2);
	CHECK(strstr(e.err.data, "out of range") != NULL);
	test_buf_reset(&e.err);
	CHECK_INT(run_jobs(&c, ARGV("--jbos", "1")), 2);
	CHECK(strstr(e.err.data, "Did you mean '--jobs'?") != NULL);
	test_buf_reset(&e.err);
	CHECK_INT(run_jobs(&c, ARGV("extra")), 2);
	CHECK(strstr(e.err.data, "unexpected argument 'extra'") != NULL);
	/* ARG_EXIT_ON_ERROR 0: the hook is never called */
	CHECK_INT(e.exit_calls, 0);
	CHECK_STR(e.out.data, "");
}

TEST(action_and_do_codes)
{
	struct env e;
	arg_config c = env_config(&e);

	CHECK_INT(run_jobs(&c, ARGV("--five")), 5);
	CHECK_INT(run_jobs(&c, ARGV("-j", "3", "--five")), 5);
	CHECK_INT(run_jobs(&c, ARGV("-X")), 1);
	CHECK(strstr(e.err.data, "refused") != NULL);
}

TEST(help_and_version_exit_0)
{
	struct env e;
	arg_config c = env_config(&e);

	CHECK_INT(run_jobs(&c, ARGV("--help")), 0);
	CHECK(strstr(e.out.data, "Usage:") != NULL);
	test_buf_reset(&e.out);
	CHECK_INT(run_jobs(&c, ARGV("-j", "7", "-h")), 0);
	CHECK(strstr(e.out.data, "Usage:") != NULL);

	/* without version_text there is no --version */
	CHECK_INT(run_jobs(&c, ARGV("--version")), 2);

	test_buf_reset(&e.out);
	c.version_text = "prog 9.9";
	CHECK_INT(run_jobs(&c, ARGV("--version")), 0);
	CHECK_STR(e.out.data, "prog 9.9\n");
	test_buf_reset(&e.out);
	CHECK_INT(run_jobs(&c, ARGV("-V")), 0);
	CHECK_STR(e.out.data, "prog 9.9\n");
	CHECK_INT(e.exit_calls, 0);
}

TEST(spec_error_exit_70)
{
	struct env e;
	arg_config c = env_config(&e);
	char *const *argv = ARGV0;

	CHECK_INT(ARG_RUN_EX(&c, 1, argv, "prog", "Test.",
		ARG_FLAG('a', "same", "One"),
		ARG_FLAG('b', "same", "Two")), 70);
	CHECK(strstr(e.err.data, "duplicate") != NULL);
}

TEST(handler_fail_message)
{
	struct env e;
	arg_config c = env_config(&e);
	char *const *argv = ARGV0;

	CHECK_INT(ARG_RUN_EX(&c, 1, argv, "prog", "Test.",
		ARG_MAIN(fail_handler)), 4);
	CHECK(strstr(e.err.data, "handler failed") != NULL);
}

TEST(subcommands_dispatch)
{
	struct env e;
	arg_config c = env_config(&e);
	char *const *argv = ARGV("sub");

	CHECK_INT(ARG_RUN_EX(&c, 2, argv, "prog", "Test.",
		ARG_CMD("sub", ret_seven, "Sub command"),
		ARG_CMD("other", NULL, "Group",
			ARG_CMD("leaf", ret_seven, "Leaf"))), 7);
	CHECK_INT(e.ran, 1);

	argv = ARGV("other");
	CHECK_INT(ARG_RUN_EX(&c, 2, argv, "prog", "Test.",
		ARG_CMD("other", NULL, "Group",
			ARG_CMD("leaf", ret_seven, "Leaf"))), 2);
	CHECK(strstr(e.err.data, "missing command") != NULL);
	CHECK_INT(e.ran, 1);
}

/* ARG_RUN() with no config: default hooks, stdout and stderr */
TEST(plain_arg_run)
{
	char *const *argv = ARGV("x");

	CHECK_INT(ARG_RUN(2, argv, "prog", "Test.",
		ARG_ARG("name", "Name"),
		ARG_MAIN(ret_seven)), 7);
	CHECK_INT(ARG_RUN(1, argv, "prog", "Test.",
		ARG_MAIN(ret_seven)), 7);
	/* a root without handler and children does nothing: exit 0 */
	CHECK_INT(ARG_RUN(1, argv, "prog", "Test.",
		ARG_FLAG('v', "verbose", "Verbose")), 0);
}

/* ------------------------------------------------------------------ */
/* Memory                                                             */
/* ------------------------------------------------------------------ */

#define BIG 100000

static char **big_argv(void)
{
	char **v = malloc((BIG + 2) * sizeof(*v));
	size_t i;

	if (!v)
		return NULL;
	v[0] = "prog";
	for (i = 1; i <= BIG; i++)
		v[i] = (i % 2) ? "odd" : "even";
	v[BIG + 1] = NULL;
	return v;
}

static int run_files(arg_config *cfg, char **argv, int argc)
{
	return ARG_RUN_EX(cfg, argc, argv, "prog", "Test.",
		ARG_FLAG('v', "verbose", "Verbose"),
		ARG_ARGS("file", "Files"),
		ARG_MAIN(count_files));
}

/* Hosted default: the heap backs the arena, nothing leaks (ASan). */
TEST(default_alloc_handles_big_argv)
{
	struct env e;
	arg_config c = env_config(&e);
	char **argv = big_argv();

	REQUIRE(argv);
	CHECK_INT(run_files(&c, argv, BIG + 1), 0);
	CHECK_INT(e.ran, 1);
	CHECK_UINT(e.nargs, BIG);
	CHECK_STR(e.err.data, "");

	/* also through ARG_RUN() without any config */
	CHECK_INT(ARG_RUN(BIG + 1, argv, "prog", "Test.",
		ARG_ARGS("file", "Files")), 0);
	free(argv);
}

/* A caller's alloc hook is used as given, and arg_free() returns all. */
TEST(caller_alloc_is_used_and_freed)
{
	struct env e;
	arg_config c = env_config(&e);
	char **argv = big_argv();

	REQUIRE(argv);
	c.alloc = count_alloc;
	c.alloc_ud = &e.cnt;
	CHECK_INT(run_files(&c, argv, BIG + 1), 0);
	CHECK_UINT(e.nargs, BIG);
	CHECK(e.cnt.calls > 0);
	CHECK_INT(e.cnt.live, 0);
	free(argv);
}

/* A caller's arena without alloc hook is not extended by the heap. */
TEST(caller_arena_is_respected)
{
	struct env e;
	arg_config c = env_config(&e);
	char **argv = big_argv();
	static uint64_t small[64];
	static uint64_t big[512 * 1024];	/* 4 MiB */

	REQUIRE(argv);
	c.arena = small;
	c.arena_cap = sizeof(small);
	CHECK_INT(run_files(&c, argv, BIG + 1), 70);
	CHECK(strstr(e.err.data, "out of memory") != NULL);
	CHECK_INT(e.ran, 0);

	/* the same arena with room for every pointer works */
	test_buf_reset(&e.err);
	c.arena = big;
	c.arena_cap = sizeof(big);
	CHECK_INT(run_files(&c, argv, BIG + 1), 0);
	CHECK_UINT(e.nargs, BIG);
	CHECK_STR(e.err.data, "");

	/* arena plus alloc hook: both are used, all blocks returned */
	c.arena = small;
	c.arena_cap = sizeof(small);
	c.alloc = count_alloc;
	c.alloc_ud = &e.cnt;
	CHECK_INT(run_files(&c, argv, BIG + 1), 0);
	CHECK(e.cnt.calls > 0);
	CHECK_INT(e.cnt.live, 0);
	free(argv);
}

/* ------------------------------------------------------------------ */
/* Exit on error (ARG_EXIT_ON_ERROR 1)                                */
/* ------------------------------------------------------------------ */

static const arg_item app[] = {
	ARG_APP("prog", "Test.",
		ARG_INT('j', "jobs", "Jobs", .def = "4", .max = "9"),
		ARG_ARGS_OPT("file", "Files"),
		ARG_MAIN(ret_jobs))
};

/* Runs arg__main(1, ...); returns -1 when the exit hook jumped out. */
static int run_exiting(struct env *e, arg_config *c, int argc,
		       char *const *argv)
{
	if (setjmp(e->jb) != 0)
		return -1;
	return arg__main(1, c, argc, argv, app);
}

TEST(exit_on_error_calls_hook_after_free)
{
	struct env e;
	arg_config c = env_config(&e);
	char **argv = big_argv();

	REQUIRE(argv);
	c.exit = jump_exit;
	c.alloc = count_alloc;
	c.alloc_ud = &e.cnt;

	/* success: no exit, the handler's code comes back */
	CHECK_INT(run_exiting(&e, &c, 3, ARGV("-j", "6")), 6);
	CHECK_INT(e.exit_calls, 0);

	/* usage error after many values: freed before the exit hook */
	argv[BIG] = "--jobs=10";
	CHECK_INT(run_exiting(&e, &c, BIG + 1, argv), -1);
	CHECK_INT(e.exit_calls, 1);
	CHECK_INT(e.exit_code, 2);
	CHECK(e.cnt.calls > 0);
	CHECK_INT(e.live_at_exit, 0);
	CHECK(strstr(e.err.data, "out of range") != NULL);

	/* help exits with 0 */
	CHECK_INT(run_exiting(&e, &c, 2, ARGV("--help")), -1);
	CHECK_INT(e.exit_calls, 2);
	CHECK_INT(e.exit_code, 0);
	CHECK(strstr(e.out.data, "Usage:") != NULL);
	free(argv);
}

TEST(exit_hook_that_returns)
{
	struct env e;
	arg_config c = env_config(&e);

	/* soft_exit returns; the code is returned after all */
	CHECK_INT(arg__main(1, &c, 2, ARGV("--bogus"), app), 2);
	CHECK_INT(e.exit_calls, 1);
	CHECK_INT(e.exit_code, 2);
	CHECK_INT(arg__main(1, &c, 2, ARGV("-j3"), app), 3);
	CHECK_INT(e.exit_calls, 1);
}

/* arg__main(0, ...) never calls the exit hook. */
TEST(no_exit_returns_status_code)
{
	struct env e;
	arg_config c = env_config(&e);

	c.exit = jump_exit;
	CHECK_INT(run_exiting(&e, &c, 2, ARGV("-j3")), 3);
	if (setjmp(e.jb) == 0)
		CHECK_INT(arg__main(0, &c, 2, ARGV("--bogus"), app), 2);
	CHECK_INT(e.exit_calls, 0);
}

TEST_MAIN(TEST_CASE(handler_return_is_exit_code),
	  TEST_CASE(usage_errors_exit_2),
	  TEST_CASE(action_and_do_codes),
	  TEST_CASE(help_and_version_exit_0),
	  TEST_CASE(spec_error_exit_70),
	  TEST_CASE(handler_fail_message),
	  TEST_CASE(subcommands_dispatch),
	  TEST_CASE(plain_arg_run),
	  TEST_CASE(default_alloc_handles_big_argv),
	  TEST_CASE(caller_alloc_is_used_and_freed),
	  TEST_CASE(caller_arena_is_respected),
	  TEST_CASE(exit_on_error_calls_hook_after_free),
	  TEST_CASE(exit_hook_that_returns),
	  TEST_CASE(no_exit_returns_status_code))
