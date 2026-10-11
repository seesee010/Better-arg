// SPDX-License-Identifier: GPL-2.0-only
/*
 * rules.c - command rules (SDD 10)
 *
 * stub, replaced by issue #7: every rule compiles and passes.
 */
#include "arg_internal.h"

arg_status arg__rules_compile(const arg_item *cmd, const arg_item *rule,
			      arg_error *err)
{
	(void)cmd;
	(void)rule;
	(void)err;
	return ARG_OK;
}

arg_status arg__rules_check(arg *p)
{
	(void)p;
	return ARG_OK;
}
