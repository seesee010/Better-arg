// SPDX-License-Identifier: GPL-2.0-only
/*
 * tokens.c - Level 2: the pull tokenizer (SDD 6.8)
 *
 * The tokenizer knows the syntax, not the spec: no items, no memory,
 * no output. The caller decides which short options take a value;
 * here only -o does, so "-vofile" is "-v" plus "-o file".
 *
 *	$ tokens -vofile --name=x pos -- --not-an-option
 *	short v
 *	short o = file
 *	long name = x
 *	positional pos
 *	end
 *	positional --not-an-option
 */
#include <stdio.h>

#include "arg.h"

int main(int argc, char **argv)
{
	arg_tokenizer tz;
	arg_tok t;
	const char *v;

	arg_tok_init(&tz, argc, argv, ARG_STYLE_GNU);
	while (arg_tok_next(&tz, &t)) {
		switch (t.kind) {
		case ARG_T_SHORT:
			if (t.ch != 'o') {
				printf("short %c\n", t.ch);
				break;
			}
			v = arg_tok_take_value(&tz);
			if (!v) {
				fprintf(stderr, "tokens: -o needs a value\n");
				return 2;
			}
			printf("short o = %s\n", v);
			break;
		case ARG_T_LONG:	/* name is not NUL-terminated */
			printf("long %.*s", (int)t.name_len, t.name);
			if (t.value)
				printf(" = %s", t.value);
			putchar('\n');
			break;
		case ARG_T_POSITIONAL:
			printf("positional %s\n", t.value);
			break;
		case ARG_T_END:		/* saw "--" */
			puts("end");
			break;
		}
	}
	return 0;
}
