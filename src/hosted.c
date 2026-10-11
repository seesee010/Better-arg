// SPDX-License-Identifier: GPL-2.0-only
/*
 * hosted.c - default hooks for hosted (non-freestanding) builds
 *
 * Everything that touches stdio or stdlib lives here, so a
 * freestanding build can leave this file out and supply hooks.
 */
#include "arg_internal.h"

#ifndef ARG_FREESTANDING

#include <stdio.h>
#include <stdlib.h>

static int write_file(FILE *f, const char *s, size_t n)
{
	if (n == 0)
		return 0;
	return fwrite(s, 1, n, f) == n ? 0 : -1;
}

int arg__write_stdout(void *ud, const char *s, size_t n)
{
	(void)ud;
	return write_file(stdout, s, n);
}

int arg__write_stderr(void *ud, const char *s, size_t n)
{
	(void)ud;
	return write_file(stderr, s, n);
}

const char *arg__getenv_default(void *ud, const char *name)
{
	(void)ud;
	return getenv(name);
}

void arg__exit_default(void *ud, int code)
{
	(void)ud;
	fflush(stdout);
	ARG_EXIT(code);
}

void *arg_alloc_stdlib(void *ud, void *ptr, size_t old_size, size_t new_size)
{
	(void)ud;
	(void)old_size;
	if (new_size == 0) {
		free(ptr);
		return NULL;
	}
	return realloc(ptr, new_size);
}

#endif /* !ARG_FREESTANDING */
