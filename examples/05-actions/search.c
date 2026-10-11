// SPDX-License-Identifier: GPL-2.0-only
/*
 * search.c - flags that run functions: ARG_ON and ARG_DO (SDD 6.5)
 *
 * ARG_ON runs at every occurrence, in argv order, while parsing goes
 * on; ARG_DO runs once after parsing instead of the handler, and its
 * return value is the exit code. ARG_RUN_EX() passes a config, here
 * only for the udata pointer the functions share.
 *
 *	$ search -I /usr/include -I . stdio.h
 *	$ search -I ../secret x       # error from arg_fail(), exit 1
 *	$ search --check              # exit 1: no search path
 */
#include <stdio.h>
#include <string.h>

#include "arg.h"

struct search {
	const char *dirs[8];
	size_t ndirs;
	int quiet;
};

/* -I DIR: collect search paths in command line order */
static int add_include(arg *a, const char *dir)
{
	struct search *s = arg_udata(a);

	if (strstr(dir, ".."))
		return arg_fail(a, "'..' is not allowed in a search path");
	if (s->ndirs == sizeof(s->dirs) / sizeof(s->dirs[0]))
		return arg_fail(a, "too many search paths");
	s->dirs[s->ndirs++] = dir;
	return 0;
}

/* -q: an ARG_ON without .value gets NULL */
static int go_quiet(arg *a, const char *unused)
{
	(void)unused;
	((struct search *)arg_udata(a))->quiet = 1;
	return 0;
}

/* --check: runs instead of search_main, exit 0 if a path was given */
static int check(arg *a)
{
	return ((struct search *)arg_udata(a))->ndirs ? 0 : 1;
}

static int search_main(arg *a)
{
	struct search *s = arg_udata(a);
	size_t i, j;

	for (i = 0; i < arg_count(a, "name"); i++)
		for (j = 0; j < s->ndirs; j++)
			if (!s->quiet)
				printf("%s/%s\n", s->dirs[j],
				       arg_str_at(a, "name", i));
	return 0;
}

int main(int argc, char **argv)
{
	struct search s = { { NULL }, 0, 0 };
	arg_config cfg = ARG_CONFIG_INIT;

	cfg.udata = &s;
	return ARG_RUN_EX(&cfg, argc, argv, "search", "Find headers.",
		ARG_ON('I', "include", add_include, "Add a search path",
		       .value = "DIR"),
		ARG_ON('q', "quiet", go_quiet, "Print nothing"),
		ARG_DO(0, "check", check, "Exit 0 if a search path is set"),
		ARG_ARGS_OPT("name", "Files to look for"),
		ARG_MAIN(search_main));
}
