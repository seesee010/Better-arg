// SPDX-License-Identifier: GPL-2.0-only
/*
 * tokenizer.c - Level 2 pull tokenizer (SDD 6.8, 7.1, 7.2)
 *
 * The tokenizer knows the command line syntax but nothing about the
 * spec. It splits argv into short option characters, long options,
 * positionals and the "--" marker; deciding whether an option takes
 * a value is left to the caller (arg_tok_take_value()).
 *
 * State, all inside struct arg_tokenizer:
 *
 *   argi  index of the next argv element that has not been looked at.
 *   pos   0 when no argument is "open". Otherwise argv[argi - 1] is
 *         still open and pos is a byte offset into it:
 *           - inside a short bundle it is the next option character
 *             ("-abc" after 'a': pos 2);
 *           - after the last character of a bundle, and after a long
 *             option without "=value", it points at the terminating
 *             NUL. The caller may then still take the next argument
 *             as the value of that option.
 *
 * argv is never written. A NULL entry before argc ends the iteration
 * like argc does.
 */
#include "arg_internal.h"

static bool tok_is_digit(char c)
{
	return c >= '0' && c <= '9';
}

/* argv[i], or NULL when i is past the end. */
static const char *tok_arg(const arg_tokenizer *tz, int i)
{
	if (i >= tz->argc)
		return NULL;
	return tz->argv[i];
}

static void tok_clear(arg_tok *t, arg_tok_kind kind, int argi, size_t offset)
{
	t->kind = kind;
	t->argi = argi;
	t->offset = offset;
	t->ch = '\0';
	t->name = NULL;
	t->name_len = 0;
	t->value = NULL;
}

void arg_tok_init(arg_tokenizer *tz, int argc, char *const *argv,
		  uint32_t style)
{
	ARG_ASSERT(tz != NULL);
	/* argc < 0, no argv or argv[0] == NULL: nothing to iterate. */
	if (argc < 0 || argv == NULL || (argc > 0 && argv[0] == NULL))
		argc = 0;
	tz->argc = argc;
	tz->argv = argv;
	tz->style = style;
	tz->argi = 1; /* skip the program name */
	tz->pos = 0;
	tz->after_dashdash = false;
	tz->posix_stopped = false;
}

/* Yield the short option character at tz->pos of argv[argi - 1]. */
static bool tok_short(arg_tokenizer *tz, arg_tok *t, const char *cur)
{
	tok_clear(t, ARG_T_SHORT, tz->argi - 1, tz->pos);
	t->ch = cur[tz->pos];
	tz->pos++;
	return true;
}

bool arg_tok_next(arg_tokenizer *tz, arg_tok *t)
{
	const char *a, *eq;

	ARG_ASSERT(tz != NULL && t != NULL);
	if (tz->pos > 0) {
		const char *cur = tz->argv[tz->argi - 1];

		if (cur[tz->pos] != '\0')
			return tok_short(tz, t, cur);
		tz->pos = 0; /* bundle or pending long option done */
	}

	a = tok_arg(tz, tz->argi);
	if (a == NULL)
		return false;
	tz->argi++;

	if (tz->after_dashdash || tz->posix_stopped)
		goto positional;

	if (a[0] == '-' && a[1] == '-') {
		if (a[2] == '\0') {
			tok_clear(t, ARG_T_END, tz->argi - 1, 0);
			tz->after_dashdash = true;
			return true;
		}
		tok_clear(t, ARG_T_LONG, tz->argi - 1, 0);
		t->name = a + 2;
		eq = strchr(t->name, '=');
		if (eq != NULL) {
			t->name_len = (size_t)(eq - t->name);
			t->value = eq + 1;
		} else {
			t->name_len = strlen(t->name);
			/* Keep it open so take_value() may use argv[argi]. */
			tz->pos = 2 + t->name_len;
		}
		return true;
	}

	if (a[0] == '-' && a[1] != '\0' &&
	    !((tz->style & ARG_TOK_NEGATIVE_NUMBERS) && tok_is_digit(a[1]))) {
		tz->pos = 1;
		return tok_short(tz, t, a);
	}

positional:
	tok_clear(t, ARG_T_POSITIONAL, tz->argi - 1, 0);
	t->value = a;
	if (tz->style & ARG_STYLE_POSIX)
		tz->posix_stopped = true;
	return true;
}

/*
 * The rest of the open short bundle, or NULL. A non-empty rest only
 * exists inside a short bundle: a pending long option has pos at its
 * terminating NUL.
 */
static const char *tok_rest(arg_tokenizer *tz)
{
	const char *cur, *rest;

	if (tz->pos == 0)
		return NULL;
	cur = tz->argv[tz->argi - 1];
	if (cur[tz->pos] == '\0')
		return NULL;
	rest = cur + tz->pos;
	tz->pos = 0;
	return rest;
}

const char *arg_tok_take_value(arg_tokenizer *tz)
{
	const char *v;

	ARG_ASSERT(tz != NULL);
	if (tz->pos == 0)
		return NULL; /* nothing open: no option to take a value */
	v = tok_rest(tz);
	if (v != NULL)
		return v;

	/* Bundle exhausted or long option without "=": next argument. */
	tz->pos = 0;
	v = tok_arg(tz, tz->argi);
	if (v != NULL)
		tz->argi++;
	return v;
}

const char *arg_tok_take_attached(arg_tokenizer *tz)
{
	ARG_ASSERT(tz != NULL);
	return tok_rest(tz);
}

void arg_tok_resume_options(arg_tokenizer *tz)
{
	ARG_ASSERT(tz != NULL);
	tz->posix_stopped = false;
}
