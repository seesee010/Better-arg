// SPDX-License-Identifier: GPL-2.0-only
/*
 * safe.c - handle errors yourself (SDD 10.3, 12)
 *
 * With ARG_EXIT_ON_ERROR 0, arg_parse() returns every status instead
 * of printing it and ending the program. The error is a struct, so
 * the program can log it, translate it or retry.
 *
 *	$ safe --port 8080
 *	$ safe --port 0               # ARG_E_RANGE, exit 2
 *	$ safe --prot 80              # ARG_E_UNKNOWN_OPTION, suggestion
 */
#define ARG_EXIT_ON_ERROR 0	/* before the include, in this file */
#include <stdio.h>

#include "arg.h"

static const arg_item app[] = {
	ARG_APP("safe", "Connects somewhere, carefully.",
		ARG_STR('H', "host", "Host name", .def = "localhost"),
		ARG_INT('p', "port", "Port", .required = true,
			.min = "1", .max = "65535"))
};

int main(int argc, char **argv)
{
	const arg_error *e;
	arg_status st;
	arg p;
	int rc = 0;

	arg_init(&p, app, NULL);
	st = arg_parse(&p, argc, argv);
	if (st == ARG_EXIT_HELP || st == ARG_EXIT_VERSION) {
		rc = arg_report(&p);	/* prints help, returns 0 */
	} else if (st != ARG_OK) {
		e = arg_error_get(&p);
		fprintf(stderr, "safe: %s", arg_status_name(e->status));
		if (e->item && e->item->name)
			fprintf(stderr, " for '%s'", e->item->name);
		if (e->text)
			fprintf(stderr, " at '%.*s'", (int)e->text_len,
				e->text);
		if (e->detail)
			fprintf(stderr, ": %s", e->detail);
		if (e->suggestion)
			fprintf(stderr, " (did you mean '%s%s'?)",
				e->suggestion_prefix, e->suggestion);
		fputc('\n', stderr);
		rc = arg_exit_code(st);
	} else {
		printf("connecting to %s:%lld\n", arg_str(&p, "host"),
		       (long long)arg_int(&p, "port"));
	}
	arg_free(&p);
	return rc;
}
