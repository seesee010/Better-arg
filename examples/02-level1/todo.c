// SPDX-License-Identifier: GPL-2.0-only
/*
 * todo.c - Level 1: a static spec and explicit control (SDD 6.7, 9.2)
 *
 * The same items as in ARG_RUN(), but in a file-scope array that can
 * live in ROM, plus a config, .to bindings into ordinary variables
 * and dispatch on the selected path instead of handlers.
 *
 *	$ todo --file work.txt add "write tests"
 *	$ todo list --limit 3
 *	$ todo stats
 *	$ todo --version
 */
#include <stdio.h>

#include "arg.h"

struct app_state {
	const char *user;
};

/* .to bindings: the parser also writes the values into these */
static const char *file;
static long limit;

static int stats(arg *a)
{
	const struct app_state *st = arg_udata(a);

	printf("%s has 0 open items in %s\n", st->user, file);
	return 0;
}

/*
 * C99: the compound literals inside ARG_APP() are constant only at
 * file scope, so the array must be declared here, not in main().
 */
static const arg_item app[] = {
	ARG_APP("todo", "Keep a list of things to do.",
		ARG_STR('f', "file", "List file",
			.def = "todo.txt", .to = ARG_TO_STR(&file)),
		ARG_CMD("add", ARG_NO_HANDLER, "Add an item",
			ARG_ARG("text", "What to do")),
		ARG_CMD("list", ARG_NO_HANDLER, "Show items",
			ARG_INT('n', "limit", "At most N items",
				.def = "10", .min = "1",
				.to = ARG_TO_LONG(&limit))),
		ARG_CMD("stats", stats, "Show statistics"))
};

int main(int argc, char **argv)
{
	struct app_state st = { "you" };
	arg_config cfg = ARG_CONFIG_INIT;
	arg p;
	int rc;

	cfg.version_text = "todo 1.0.0";
	cfg.udata = &st;

	arg_init(&p, app, &cfg);
	if (arg_parse(&p, argc, argv) != ARG_OK) {
		/* only reached with ARG_EXIT_ON_ERROR 0 */
		rc = arg_report(&p);
	} else if (arg_path_is(&p, "add")) {
		printf("added \"%s\" to %s\n", arg_str(&p, "text"), file);
		rc = 0;
	} else if (arg_path_is(&p, "list")) {
		printf("showing up to %ld items of %s\n", limit, file);
		rc = 0;
	} else {
		rc = arg_run(&p);	/* commands that have a handler */
	}
	arg_free(&p);
	return rc;
}
