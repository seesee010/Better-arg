// SPDX-License-Identifier: GPL-2.0-only
/*
 * run.c - Level 0: ARG_RUN() and ARG_RUN_EX() (SDD 6.1, 10.3)
 *
 * The whole Level 0 layer is this one function. It only calls the
 * public Level 1 API (G2): init, parse, report or run, free. The
 * parser lives on its stack, so a program written with ARG_RUN()
 * never sees a parser, a config or a free call.
 */
#include "arg_internal.h"

/*
 * Parse argv against the root command and dispatch.
 *
 * Returns the exit code: 0 for --help and --version, arg_exit_code()
 * of the status for errors (2 for usage errors) and otherwise the
 * value of the ARG_DO function or command handler that ran.
 *
 * Exit on error is done here rather than inside arg_parse(): the
 * parser is initialised with exit_on_error 0 and the exit hook is
 * called only after arg_free(), so no cfg.alloc block is lost when
 * the program ends (or when a test hook longjmps out). The output is
 * the same, because arg_parse() would print through arg_report() too.
 */
int arg__main(int exit_on_error, const arg_config *cfg, int argc,
	      char *const *argv, const arg_item *root)
{
	arg_config c = ARG_CONFIG_INIT;
	arg_exit_fn quit;
	void *quit_ud;
	arg_status st;
	arg p;
	int code;

	if (cfg)
		c = *cfg;
#ifndef ARG_FREESTANDING
	/*
	 * The inline arena holds a few dozen values; a hosted program
	 * that did not choose its memory gets the heap behind it, so a
	 * long command line (xargs, globs) never fails with NO_MEMORY.
	 */
	if (!c.alloc && !c.arena)
		c.alloc = arg_alloc_stdlib;
#endif
	arg__init(&p, root, &c, 0);
	st = arg_parse(&p, argc, argv);
	if (st == ARG_OK)
		code = arg_run(&p);
	else
		code = arg_report(&p);

	/* defaults are filled in by arg__init(); arg_free() keeps them */
	quit = arg__priv(&p)->cfg.exit;
	quit_ud = arg__priv(&p)->cfg.exit_ud;
	arg_free(&p);

	if (st != ARG_OK && exit_on_error && quit)
		quit(quit_ud, code);
	return code;
}
