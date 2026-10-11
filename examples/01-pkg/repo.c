// SPDX-License-Identifier: GPL-2.0-only
/*
 * repo.c - the "pkg repo" subtree, kept in its own file (SDD 6.6)
 *
 * ARG_DEFINE() gives the tree a name, ARG_EXTERN() in pkg.h declares
 * it and ARG_USE() in main.c splices it in. A tool with dozens of
 * commands keeps one short main() and one file per command family.
 */
#include <stdio.h>

#include "pkg.h"

static int repo_add(arg *a)
{
	if (arg_flag(a, "verbose"))	/* global flag, declared in main.c */
		puts("adding repo ...");
	printf("%s -> %s\n", arg_str(a, "name"), arg_str(a, "url"));
	return 0;
}

static int repo_list(arg *a)
{
	(void)a;
	puts("main https://example.org/main");
	return 0;
}

/* NULL instead of a handler: "repo" only groups, a subcommand is needed */
ARG_DEFINE(repo_cmd,
	ARG_CMD("repo", NULL, "Manage repositories",
		ARG_CMD("add", repo_add, "Add a repository",
			ARG_ARG("name", "Repository name"),
			ARG_ARG("url", "Repository URL")),
		ARG_CMD("list", repo_list, "List repositories",
			ARG_ALIAS("ls"))));
