// SPDX-License-Identifier: GPL-2.0-only
/*
 * error.c - status codes, message table and error output (SDD 12, 15)
 *
 * Messages are formatted lazily, only when an error is printed. The
 * formatter knows exactly two directives, %s and %%; the template comes
 * from the message table and user text is only ever substituted into
 * it, never interpreted. Echoed user input is escaped so that control
 * bytes and broken UTF-8 cannot reach the terminal (SDD 15).
 */
#include "arg_internal.h"

/* ------------------------------------------------------------------ */
/* Status codes                                                       */
/* ------------------------------------------------------------------ */

#define STATUS_NAME(st) [st] = #st

static const char *const status_names[ARG_STATUS_COUNT] = {
	STATUS_NAME(ARG_OK),
	STATUS_NAME(ARG_EXIT_HELP),
	STATUS_NAME(ARG_EXIT_VERSION),
	STATUS_NAME(ARG_E_UNKNOWN_OPTION),
	STATUS_NAME(ARG_E_AMBIGUOUS),
	STATUS_NAME(ARG_E_MISSING_VALUE),
	STATUS_NAME(ARG_E_UNEXPECTED_VALUE),
	STATUS_NAME(ARG_E_BAD_VALUE),
	STATUS_NAME(ARG_E_RANGE),
	STATUS_NAME(ARG_E_REPEATED),
	STATUS_NAME(ARG_E_MISSING_REQUIRED),
	STATUS_NAME(ARG_E_TOO_MANY_ARGS),
	STATUS_NAME(ARG_E_TOO_FEW_ARGS),
	STATUS_NAME(ARG_E_UNKNOWN_COMMAND),
	STATUS_NAME(ARG_E_MISSING_COMMAND),
	STATUS_NAME(ARG_E_RULE),
	STATUS_NAME(ARG_E_NO_MEMORY),
	STATUS_NAME(ARG_E_IO),
	STATUS_NAME(ARG_E_SPEC),
	STATUS_NAME(ARG_E_ACTION),
};

#undef STATUS_NAME

static bool status_valid(arg_status st)
{
	return (unsigned)st < (unsigned)ARG_STATUS_COUNT;
}

const char *arg_status_name(arg_status st)
{
	if (!status_valid(st))
		return "ARG_E_UNKNOWN_STATUS";
	return status_names[st];
}

int arg_exit_code(arg_status st)
{
	switch (st) {
	case ARG_OK:
	case ARG_EXIT_HELP:
	case ARG_EXIT_VERSION:
		return 0;
	case ARG_E_IO:
		return 66; /* EX_NOINPUT */
	case ARG_E_NO_MEMORY:
	case ARG_E_SPEC:
		return 70; /* EX_SOFTWARE */
	case ARG_E_ACTION:
		return 1;
	case ARG_E_UNKNOWN_OPTION:
	case ARG_E_AMBIGUOUS:
	case ARG_E_MISSING_VALUE:
	case ARG_E_UNEXPECTED_VALUE:
	case ARG_E_BAD_VALUE:
	case ARG_E_RANGE:
	case ARG_E_REPEATED:
	case ARG_E_MISSING_REQUIRED:
	case ARG_E_TOO_MANY_ARGS:
	case ARG_E_TOO_FEW_ARGS:
	case ARG_E_UNKNOWN_COMMAND:
	case ARG_E_MISSING_COMMAND:
	case ARG_E_RULE:
		return 2;
	case ARG_STATUS_COUNT:
	default:
		break;
	}
	return 70;
}

/* ------------------------------------------------------------------ */
/* Message table                                                      */
/* ------------------------------------------------------------------ */

const arg_messages arg_default_messages = { {
	[ARG_MSG_UNKNOWN_OPTION] = "unknown option '%s'",
	[ARG_MSG_AMBIGUOUS] = "option '%s' is ambiguous",
	[ARG_MSG_MISSING_VALUE] = "option '%s' requires a value",
	[ARG_MSG_UNEXPECTED_VALUE] = "option '%s' doesn't take a value",
	[ARG_MSG_BAD_VALUE] = "invalid value '%s' for '%s'",
	[ARG_MSG_RANGE] = "value '%s' for '%s' is out of range",
	[ARG_MSG_REPEATED] = "option '%s' may only be given once",
	[ARG_MSG_MISSING_REQUIRED] = "missing required option '%s'",
	[ARG_MSG_TOO_MANY_ARGS] = "unexpected argument '%s'",
	[ARG_MSG_TOO_FEW_ARGS] = "missing required argument %s",
	[ARG_MSG_UNKNOWN_COMMAND] = "unknown command '%s'",
	[ARG_MSG_MISSING_COMMAND] = "missing command",
	[ARG_MSG_RULE] = "%s",
	[ARG_MSG_NO_MEMORY] = "out of memory",
	[ARG_MSG_IO] = "cannot read '%s'",
	[ARG_MSG_SPEC] = "invalid specification: %s",
	[ARG_MSG_ACTION] = "%s",
	[ARG_MSG_DID_YOU_MEAN] = "Did you mean '%s'?",
	[ARG_MSG_TRY_HELP] = "Try '%s --help' for more information.",
	[ARG_MSG_USAGE] = "Usage:",
	[ARG_MSG_ARGUMENTS] = "Arguments:",
	[ARG_MSG_OPTIONS] = "Options:",
	[ARG_MSG_COMMANDS] = "Commands:",
	[ARG_MSG_EXAMPLES] = "Examples:",
	[ARG_MSG_HELP_FLAG] = "Print help",
	[ARG_MSG_VERSION_FLAG] = "Print version",
	[ARG_MSG_DEFAULT] = "default",
	[ARG_MSG_ENV] = "env",
	[ARG_MSG_POSSIBLE] = "possible",
	[ARG_MSG_SEE_COMMAND_HELP] = "See '%s <command> --help' for more.",
	[ARG_MSG_DEPRECATED] = "option '%s' is deprecated: %s",
} };

const char *arg__msg(const arg_messages *m, arg_msg_id id)
{
	if ((unsigned)id >= (unsigned)ARG_MSG_COUNT)
		return "";
	if (m && m->text[id])
		return m->text[id];
	return arg_default_messages.text[id];
}

/* ------------------------------------------------------------------ */
/* Low-level writers                                                  */
/* ------------------------------------------------------------------ */

static int put(arg_write_fn w, void *ud, const char *s, size_t n)
{
	return n ? w(ud, s, n) : 0;
}

static int puts0(arg_write_fn w, void *ud, const char *s)
{
	return s ? put(w, ud, s, strlen(s)) : 0;
}

static bool in_range(unsigned char c, unsigned lo, unsigned hi)
{
	return c >= lo && c <= hi;
}

/*
 * Length of the valid, printable UTF-8 sequence at s[0..n), or 0 when
 * the first byte must be escaped. Rejects C0 controls, DEL, overlong
 * forms, surrogates, code points above U+10FFFF and truncated input.
 */
static size_t utf8_len(const unsigned char *s, size_t n)
{
	unsigned char c = s[0];
	unsigned lo = 0x80, hi = 0xbf;
	size_t len, i;

	if (c < 0x80)
		return c < 0x20 || c == 0x7f ? 0u : 1u;
	if (in_range(c, 0xc2, 0xdf)) {
		len = 2;
	} else if (in_range(c, 0xe0, 0xef)) {
		len = 3;
		if (c == 0xe0)
			lo = 0xa0; /* overlong */
		else if (c == 0xed)
			hi = 0x9f; /* surrogates */
	} else if (in_range(c, 0xf0, 0xf4)) {
		len = 4;
		if (c == 0xf0)
			lo = 0x90; /* overlong */
		else if (c == 0xf4)
			hi = 0x8f; /* > U+10FFFF */
	} else {
		return 0; /* continuation byte, C0/C1 overlong, F5..FF */
	}
	if (n < len || !in_range(s[1], lo, hi))
		return 0;
	for (i = 2; i < len; i++)
		if (!in_range(s[i], 0x80, 0xbf))
			return 0;
	return len;
}

int arg__write_escaped(arg_write_fn w, void *ud, const char *s, size_t n)
{
	static const char hex[] = "0123456789ABCDEF";
	const unsigned char *u = (const unsigned char *)s;
	size_t i = 0, run = 0, len;
	char esc[4];
	int r;

	if (!s)
		return 0;
	while (i < n) {
		len = utf8_len(u + i, n - i);
		if (len) {
			i += len;
			continue;
		}
		r = put(w, ud, s + run, i - run);
		if (r)
			return r;
		esc[0] = '\\';
		esc[1] = 'x';
		esc[2] = hex[u[i] >> 4];
		esc[3] = hex[u[i] & 0x0f];
		r = w(ud, esc, sizeof(esc));
		if (r)
			return r;
		run = ++i;
	}
	return put(w, ud, s + run, n - run);
}

int arg__write_path(arg_write_fn w, void *ud, const struct arg__path *path)
{
	const char *root = NULL;
	size_t i;
	int r;

	if (path && path->prog)
		root = path->prog;
	else if (path && path->cmds && path->depth && path->cmds[0])
		root = path->cmds[0]->name;
	/* prog usually comes from argv[0]: echo it escaped */
	if (root)
		r = arg__write_escaped(w, ud, root, strlen(root));
	else
		r = put(w, ud, "?", 1);
	if (r || !path || !path->cmds)
		return r;
	for (i = 1; i < path->depth; i++) {
		const arg_item *c = path->cmds[i];

		r = put(w, ud, " ", 1);
		if (!r)
			r = puts0(w, ud, c && c->name ? c->name : "?");
		if (r)
			return r;
	}
	return 0;
}

static bool is_option(arg_item_kind k)
{
	return k == ARG_I_FLAG || k == ARG_I_COUNT || k == ARG_I_VALUE ||
	       k == ARG_I_DO || k == ARG_I_ON;
}

static int write_option_name(arg_write_fn w, void *ud, const arg_item *it)
{
	char sh[2];
	int r;

	if (it->name && it->name[0]) {
		r = put(w, ud, "--", 2);
		return r ? r : puts0(w, ud, it->name);
	}
	if (!it->short_name)
		return put(w, ud, "?", 1);
	sh[0] = '-';
	sh[1] = it->short_name;
	return put(w, ud, sh, sizeof(sh));
}

static int write_metavar(arg_write_fn w, void *ud, const arg_item *it)
{
	const char *mv = "VALUE";
	int r;

	if (it->value)
		mv = it->value;
	else if (it->type && it->type->name)
		mv = it->type->name;
	if (it->optional_value)
		r = put(w, ud, "[=<", 3);
	else
		r = put(w, ud, " <", 2);
	if (!r)
		r = puts0(w, ud, mv);
	if (!r)
		r = put(w, ud, it->optional_value ? ">]" : ">",
			it->optional_value ? 2 : 1);
	return r;
}

int arg__write_item_label(arg_write_fn w, void *ud, const arg_item *it,
			  bool with_metavar)
{
	int r;

	if (!it)
		return put(w, ud, "?", 1);
	if (is_option(it->kind)) {
		r = write_option_name(w, ud, it);
		if (r || !with_metavar)
			return r;
		if (it->kind == ARG_I_VALUE ||
		    (it->kind == ARG_I_ON && it->value))
			return write_metavar(w, ud, it);
		return 0;
	}
	if (it->kind == ARG_I_ARG) {
		r = put(w, ud, "<", 1);
		if (!r)
			r = puts0(w, ud, it->name ? it->name : "?");
		if (!r)
			r = put(w, ud, ">", 1);
		if (!r && it->many)
			r = put(w, ud, "...", 3);
		return r;
	}
	return puts0(w, ud, it->name ? it->name : "?");
}

/* ------------------------------------------------------------------ */
/* Formatter                                                          */
/* ------------------------------------------------------------------ */

/*
 * One %s slot of a template may be filled by something that is not a
 * plain string: an item label, the command path or a suggestion with
 * its prefix. Writing those directly avoids any intermediate buffer.
 */
enum slot_kind {
	SLOT_NONE = 0,
	SLOT_LABEL,         /* item label without metavar */
	SLOT_LABEL_METAVAR, /* item label with metavar    */
	SLOT_PATH,          /* command path               */
	SLOT_SUGGEST        /* prefix + suggestion        */
};

struct slot {
	enum slot_kind kind;
	size_t index; /* which %s it replaces */
	const arg_item *item;
	const struct arg__path *path;
	const char *prefix, *name;
};

static int write_slot(arg_write_fn w, void *ud, const struct slot *sl)
{
	int r;

	switch (sl->kind) {
	case SLOT_LABEL:
	case SLOT_LABEL_METAVAR:
		return arg__write_item_label(w, ud, sl->item,
					     sl->kind == SLOT_LABEL_METAVAR);
	case SLOT_PATH:
		return arg__write_path(w, ud, sl->path);
	case SLOT_SUGGEST:
		r = puts0(w, ud, sl->prefix);
		return r ? r : puts0(w, ud, sl->name);
	case SLOT_NONE:
	default:
		break;
	}
	return 0;
}

static int write_arg(arg_write_fn w, void *ud, const struct arg__str *a)
{
	if (!a->s)
		return 0;
	if (a->untrusted)
		return arg__write_escaped(w, ud, a->s, a->n);
	return put(w, ud, a->s, a->n);
}

static int format_core(arg_write_fn w, void *ud, const char *fmt,
		       const struct arg__str *args, size_t nargs,
		       const struct slot *sl)
{
	const char *run = fmt, *p = fmt;
	size_t next = 0;
	int r;

	if (!fmt)
		return 0;
	while (*p) {
		if (p[0] != '%' || (p[1] != 's' && p[1] != '%')) {
			p++; /* literal, including unknown %x */
			continue;
		}
		if (p[1] == '%') {
			/* keep the first '%' in the run, drop the second */
			r = put(w, ud, run, (size_t)(p + 1 - run));
			if (r)
				return r;
			p += 2;
			run = p;
			continue;
		}
		r = put(w, ud, run, (size_t)(p - run));
		if (r)
			return r;
		if (sl && sl->kind != SLOT_NONE && sl->index == next)
			r = write_slot(w, ud, sl);
		else if (args && next < nargs)
			r = write_arg(w, ud, &args[next]);
		if (r)
			return r;
		next++;
		p += 2;
		run = p;
	}
	return put(w, ud, run, (size_t)(p - run));
}

int arg__format(arg_write_fn w, void *ud, const char *fmt,
		const struct arg__str *args, size_t nargs)
{
	return format_core(w, ud, fmt, args, nargs, NULL);
}

/* ------------------------------------------------------------------ */
/* Error report                                                       */
/* ------------------------------------------------------------------ */

static arg_msg_id status_msg(arg_status st)
{
	switch (st) {
	case ARG_E_UNKNOWN_OPTION:
		return ARG_MSG_UNKNOWN_OPTION;
	case ARG_E_AMBIGUOUS:
		return ARG_MSG_AMBIGUOUS;
	case ARG_E_MISSING_VALUE:
		return ARG_MSG_MISSING_VALUE;
	case ARG_E_UNEXPECTED_VALUE:
		return ARG_MSG_UNEXPECTED_VALUE;
	case ARG_E_BAD_VALUE:
		return ARG_MSG_BAD_VALUE;
	case ARG_E_RANGE:
		return ARG_MSG_RANGE;
	case ARG_E_REPEATED:
		return ARG_MSG_REPEATED;
	case ARG_E_MISSING_REQUIRED:
		return ARG_MSG_MISSING_REQUIRED;
	case ARG_E_TOO_MANY_ARGS:
		return ARG_MSG_TOO_MANY_ARGS;
	case ARG_E_TOO_FEW_ARGS:
		return ARG_MSG_TOO_FEW_ARGS;
	case ARG_E_UNKNOWN_COMMAND:
		return ARG_MSG_UNKNOWN_COMMAND;
	case ARG_E_MISSING_COMMAND:
		return ARG_MSG_MISSING_COMMAND;
	case ARG_E_RULE:
		return ARG_MSG_RULE;
	case ARG_E_NO_MEMORY:
		return ARG_MSG_NO_MEMORY;
	case ARG_E_IO:
		return ARG_MSG_IO;
	case ARG_E_SPEC:
		return ARG_MSG_SPEC;
	case ARG_E_ACTION:
		return ARG_MSG_ACTION;
	default:
		break;
	}
	return ARG_MSG_COUNT;
}

/*
 * The message part of the first line, without the path. Sets
 * *detail_used when e->detail already went into the message.
 */
static int write_message(arg_write_fn w, void *ud, const arg_error *e,
			 const arg_messages *m, bool *detail_used)
{
	struct arg__str a = { NULL, 0, true };
	struct slot sl = { SLOT_NONE, 0, NULL, NULL, NULL, NULL };
	arg_msg_id id = status_msg(e->status);
	const char *fallback = NULL;

	if (e->text) {
		a.s = e->text;
		a.n = e->text_len ? e->text_len : strlen(e->text);
	}
	sl.item = e->item;
	switch (e->status) {
	case ARG_E_MISSING_VALUE:
	case ARG_E_UNEXPECTED_VALUE:
	case ARG_E_REPEATED:
	case ARG_E_MISSING_REQUIRED:
	case ARG_E_TOO_FEW_ARGS:
		sl.kind = SLOT_LABEL;
		break;
	case ARG_E_BAD_VALUE:
	case ARG_E_RANGE:
		sl.kind = SLOT_LABEL_METAVAR;
		sl.index = 1;
		break;
	case ARG_E_RULE:
		fallback = "rule violated";
		break;
	case ARG_E_ACTION:
		fallback = "action failed";
		break;
	case ARG_E_SPEC:
		fallback = "unknown error";
		break;
	default:
		break;
	}
	if (fallback) {
		/* the detail is the message; it is programmer text */
		a.s = e->detail ? e->detail : fallback;
		a.n = strlen(a.s);
		a.untrusted = false;
		*detail_used = true;
	}
	if (id == ARG_MSG_COUNT)
		return puts0(w, ud, arg_status_name(e->status));
	return format_core(w, ud, arg__msg(m, id), &a, 1, &sl);
}

int arg__write_error(arg_write_fn w, void *ud, const arg_error *e,
		     const struct arg__path *path, const arg_messages *m)
{
	struct slot sl = { SLOT_NONE, 0, NULL, NULL, NULL, NULL };
	bool detail_used = false;
	int r;

	if (!e || e->status == ARG_OK || e->status == ARG_EXIT_HELP ||
	    e->status == ARG_EXIT_VERSION)
		return 0;

	r = arg__write_path(w, ud, path);
	if (!r)
		r = put(w, ud, ": ", 2);
	if (!r)
		r = write_message(w, ud, e, m, &detail_used);
	if (!r && e->detail && !detail_used) {
		r = put(w, ud, ": ", 2);
		if (!r)
			r = puts0(w, ud, e->detail);
	}
	if (!r)
		r = put(w, ud, "\n", 1);
	if (r)
		return r;

	if (e->suggestion) {
		sl.kind = SLOT_SUGGEST;
		sl.prefix = e->suggestion_prefix ? e->suggestion_prefix : "";
		sl.name = e->suggestion;
		r = put(w, ud, "  ", 2);
		if (!r)
			r = format_core(w, ud,
					arg__msg(m, ARG_MSG_DID_YOU_MEAN), NULL,
					0, &sl);
		if (!r)
			r = put(w, ud, "\n", 1);
		if (r)
			return r;
	}

	if (arg_exit_code(e->status) == 2) {
		sl.kind = SLOT_PATH;
		sl.path = path;
		r = format_core(w, ud, arg__msg(m, ARG_MSG_TRY_HELP), NULL, 0,
				&sl);
		if (!r)
			r = put(w, ud, "\n", 1);
	}
	return r;
}
