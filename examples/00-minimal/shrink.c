// SPDX-License-Identifier: GPL-2.0-only
/*
 * shrink.c - the smallest Better-arg program (SDD 6.1)
 *
 * One call declares the options, parses argv, prints help and errors
 * and runs the handler. No structs, no variables, no free.
 *
 *	$ shrink -v -q 50 a.png b.png
 *	$ shrink --quality=101 a.png     # error, exit code 2
 */
#include <stdio.h>

#include "arg.h"

/* Handler of the root command; its return value is the exit code. */
static int shrink_main(arg *a)
{
	size_t i;

	for (i = 0; i < arg_count(a, "image"); i++)
		printf("%s%s at quality %lld\n",
		       arg_flag(a, "verbose") ? "shrinking " : "",
		       arg_str_at(a, "image", i),
		       (long long)arg_int(a, "quality"));
	return 0;
}

int main(int argc, char **argv)
{
	return ARG_RUN(argc, argv, "shrink", "Makes images smaller.",
		ARG_FLAG('v', "verbose", "Print what happens"),
		ARG_INT('q', "quality", "Quality",
			.def = "80", .min = "1", .max = "100"),
		ARG_ARGS("image", "Images to shrink"),
		ARG_MAIN(shrink_main));
}
