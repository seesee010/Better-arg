// SPDX-License-Identifier: GPL-2.0-only
/*
 * main.c - pkg, a tiny package manager front end (SDD 6.1)
 *
 * The whole command tree is one nested declaration: commands are
 * items that contain items, handlers read values by name.
 *
 *	$ pkg install -f vim git          # or: pkg i ...
 *	$ PKG_ROOT=/opt pkg install vim    # .env fallback for --root
 *	$ pkg -v repo add main https://example.org
 *	$ pkg --list-formats               # ARG_DO: runs, then exits
 */
#include <stdio.h>

#include "pkg.h"

static int install(arg *a)
{
	size_t i;

	for (i = 0; i < arg_count(a, "name"); i++)
		printf("install %s into %s%s\n", arg_str_at(a, "name", i),
		       arg_str(a, "root"),
		       arg_flag(a, "force") ? " (forced)" : "");
	return 0;	/* becomes the exit code */
}

static int remove_pkgs(arg *a)
{
	size_t i;

	for (i = 0; i < arg_count(a, "name"); i++)
		printf("remove %s\n", arg_str_at(a, "name", i));
	return 0;
}

static int list_formats(arg *a)
{
	(void)a;
	puts("tar zip zst");
	return 0;
}

int main(int argc, char **argv)
{
	return ARG_RUN(argc, argv, "pkg", "A tiny package manager.",
		ARG_FLAG('v', "verbose", "Print more", .global = true),
		ARG_STR(0, "root", "Install prefix",
			.def = "/", .env = "PKG_ROOT"),
		ARG_DO(0, "list-formats", list_formats,
		       "Show formats and exit"),

		ARG_CMD("install", install, "Install packages",
			ARG_ALIAS("i"),
			ARG_FLAG('f', "force", "Reinstall even if present"),
			ARG_ARGS("name", "Packages to install")),

		ARG_CMD("remove", remove_pkgs, "Remove packages",
			ARG_ALIAS("rm"),
			ARG_ARGS("name", "Packages to remove")),

		ARG_USE(repo_cmd));
}
