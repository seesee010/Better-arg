// SPDX-License-Identifier: GPL-2.0-only
/*
 * version.c - library version and small always-present helpers
 */
#include "arg_internal.h"

const char *arg_version(void)
{
	return ARG_VERSION_STRING;
}

int arg_no_handler(arg *a)
{
	(void)a;
	return 0;
}
