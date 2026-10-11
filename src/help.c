// SPDX-License-Identifier: GPL-2.0-only
/*
 * help.c - help and usage output (SDD 11.1)
 *
 *	Usage: mytool [OPTIONS] <INPUT>...
 *
 *	Compiles things quickly.
 *
 *	Arguments:
 *	  <INPUT>...              Input files
 *
 *	Options:
 *	  -o, --output <FILE>     Output file [default: out.bin]
 *	  -h, --help              Print help
 *
 *	Commands:
 *	  build    Build the project
 *
 *	See 'mytool <command> --help' for more.
 *
 * Everything is written straight from the spec through a 256-byte
 * stack buffer; nothing is allocated. Column widths count UTF-8 code
 * points. Lines stay shorter than the width, so a terminal never
 * wraps them on its own at the last column.
 *
 * Spec text (help, names) is written verbatim; the program name comes
 * from argv[0] and is escaped by arg__write_path().
 */
#include "arg_internal.h"

#define HELP_BUF 256   /* output chunk size (SDD 14)                    */
#define HELP_WORD 128  /* longest word kept for a wrap decision         */
#define HELP_INDENT 2  /* rows start here                               */
#define HELP_GAP 3     /* left cell to description, option table        */
#define HELP_CMD_GAP 4 /* left cell to description, command table       */
#define HELP_MIN_GAP 2 /* less than this puts the text on the next line */
#define HELP_WIDTH 80

/* ------------------------------------------------------------------ */
/* Buffered output                                                    */
/* ------------------------------------------------------------------ */

struct out {
	arg_write_fn w;
	void *ud;
	int err;  /* first failed write, then everything is dropped */
	size_t n;
	char buf[HELP_BUF];
};

static void out_flush(struct out *o)
{
	if (!o->err && o->n)
		o->err = o->w(o->ud, o->buf, o->n);
	o->n = 0;
}

/* arg_write_fn over a struct out. */
static int out_write(void *ud, const char *s, size_t n)
{
	struct out *o = ud;
	size_t k;

	while (n && !o->err) {
		if (o->n == sizeof(o->buf))
			out_flush(o);
		k = sizeof(o->buf) - o->n;
		if (k > n)
			k = n;
		memcpy(o->buf + o->n, s, k);
		o->n += k;
		s += k;
		n -= k;
	}
	return o->err;
}

static void out_str(struct out *o, const char *s)
{
	if (s)
		out_write(o, s, strlen(s));
}

static void out_char(struct out *o, char c)
{
	out_write(o, &c, 1);
}

static void out_spaces(struct out *o, size_t n)
{
	while (n--)
		out_char(o, ' ');
}

/* Display width: UTF-8 continuation bytes take no column. */
static size_t cols_of(const char *s, size_t n)
{
	size_t i, c = 0;

	for (i = 0; i < n; i++)
		c += ((unsigned char)s[i] & 0xc0) != 0x80;
	return c;
}

/* arg_write_fn that only measures; ud is a size_t column count. */
static int meter_write(void *ud, const char *s, size_t n)
{
	*(size_t *)ud += cols_of(s, n);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Word wrapping                                                      */
/* ------------------------------------------------------------------ */

/*
 * Text is fed in pieces through wrap_write(). Words are separated by
 * spaces; a unit (an annotation such as "[default: a b]") is one word
 * spaces included. Before the first word the cursor moves to the
 * indent: right there when the gap is wide enough, else on a new line.
 * A word too long for the buffer is written as is, unbroken.
 */
struct wrap {
	struct out *o;
	size_t indent, limit;
	size_t col;    /* cursor column                      */
	bool started;  /* first word written                 */
	bool unit;     /* spaces belong to the word          */
	bool spilled;  /* part of this word is written       */
	size_t wlen, wcols;
	char word[HELP_WORD];
};

static void wrap_init(struct wrap *wr, struct out *o, size_t indent,
		      size_t col, size_t limit)
{
	wr->o = o;
	wr->indent = indent;
	wr->limit = limit;
	wr->col = col;
	wr->started = false;
	wr->unit = false;
	wr->spilled = false;
	wr->wlen = 0;
	wr->wcols = 0;
}

static void wrap_newline(struct wrap *wr)
{
	out_char(wr->o, '\n');
	wr->col = 0;
}

/* Move the cursor to where a word of cols columns goes. */
static void wrap_place(struct wrap *wr, size_t cols)
{
	if (wr->col && !wr->started) {
		/* after the left cell: same line if the gap is wide enough */
		if (wr->col + HELP_MIN_GAP > wr->indent)
			wrap_newline(wr);
	} else if (wr->col && wr->col + 1 + cols > wr->limit) {
		wrap_newline(wr);
	} else if (wr->col) {
		out_char(wr->o, ' ');
		wr->col++;
	}
	if (wr->col < wr->indent) {
		out_spaces(wr->o, wr->indent - wr->col);
		wr->col = wr->indent;
	}
	wr->started = true;
}

static void wrap_word_out(struct wrap *wr)
{
	if (!wr->spilled)
		wrap_place(wr, wr->wcols);
	out_write(wr->o, wr->word, wr->wlen);
	wr->col += wr->wcols;
	wr->wlen = 0;
	wr->wcols = 0;
}

static void wrap_word_end(struct wrap *wr)
{
	if (wr->wlen)
		wrap_word_out(wr);
	wr->spilled = false;
}

/* arg_write_fn over a struct wrap. */
static int wrap_write(void *ud, const char *s, size_t n)
{
	struct wrap *wr = ud;
	size_t i;
	char c;

	for (i = 0; i < n; i++) {
		c = s[i];
		if (c == '\n') {
			wrap_word_end(wr);
			if (wr->started)
				wrap_newline(wr);
			continue;
		}
		if (c == ' ' && !wr->unit) {
			wrap_word_end(wr);
			continue;
		}
		if (wr->wlen == sizeof(wr->word)) {
			wrap_word_out(wr);
			wr->spilled = true;
		}
		wr->word[wr->wlen++] = c;
		wr->wcols += ((unsigned char)c & 0xc0) != 0x80;
	}
	return wr->o->err;
}

static void wrap_str(struct wrap *wr, const char *s)
{
	if (s)
		wrap_write(wr, s, strlen(s));
}

static void wrap_unit_begin(struct wrap *wr)
{
	wrap_word_end(wr);
	wr->unit = true;
}

static void wrap_unit_end(struct wrap *wr)
{
	wrap_word_end(wr);
	wr->unit = false;
}

/* ------------------------------------------------------------------ */
/* Spec helpers                                                       */
/* ------------------------------------------------------------------ */

struct help {
	struct out o;
	const arg_messages *m;
	const struct arg__pathbuf *pb;
	const arg_item *cmd;
	size_t width, limit;
	size_t col;     /* description column of the option table */
	size_t cmd_col; /* description column of the command table */
	arg_item auto_help, auto_version; /* kind END = not shown */
};

static bool is_option(const arg_item *it)
{
	switch (it->kind) {
	case ARG_I_FLAG:
	case ARG_I_COUNT:
	case ARG_I_VALUE:
	case ARG_I_DO:
	case ARG_I_ON:
		return true;
	default:
		return false;
	}
}

static bool shown_option(const arg_item *it)
{
	return is_option(it) && !it->hidden;
}

static bool is_word(const char *s, const char *w)
{
	return s && strcmp(s, w) == 0;
}

/* Long name or alias equals w. */
static bool is_named(const arg_item *it, const char *w)
{
	return is_word(it->name, w) || is_word(it->alias, w);
}

static bool has_handler(const arg_item *cmd)
{
	struct arg__iter it;
	const arg_item *x;

	if (cmd->fn.run)
		return true;
	arg__iter_init(&it, cmd);
	while ((x = arg__iter_next(&it)))
		if (x->kind == ARG_I_MAIN && x->fn.run)
			return true;
	return false;
}

/* Choices listed as "[possible: ...]", NULL when there are none. */
static const char *const *choices_of(const arg_item *it)
{
	const arg_type *t;

	if (it->kind != ARG_I_VALUE && it->kind != ARG_I_ARG)
		return NULL;
	t = arg__value_type(it);
	if (t && t->kind == ARG_K_ENUM)
		return it->names;
	if (t && t->parse == arg_one_of_parse)
		return (const char *const *)t->ud;
	return NULL;
}

/*
 * Options of the command and the .global options of its ancestors,
 * ancestors first, in the order they are listed: calls fn for every
 * option, hidden ones included.
 */
struct opt_walk {
	const struct arg__pathbuf *pb;
	size_t level;          /* index into pb->cmds */
	struct arg__iter it;
};

static void walk_init(struct opt_walk *ow, const struct arg__pathbuf *pb)
{
	ow->pb = pb;
	ow->level = 0;
	arg__iter_init(&ow->it, pb->cmds[0]);
}

/* Next reachable option; *own tells whether the command declares it. */
static const arg_item *walk_next(struct opt_walk *ow, bool *own)
{
	size_t last = ow->pb->path.depth - 1;
	const arg_item *x;

	for (;;) {
		while ((x = arg__iter_next(&ow->it))) {
			if (!is_option(x))
				continue;
			if (ow->level == last || x->global) {
				*own = ow->level == last;
				return x;
			}
		}
		if (ow->level == last)
			return NULL;
		ow->level++;
		arg__iter_init(&ow->it, ow->pb->cmds[ow->level]);
	}
}

/*
 * The automatic -h/--help and -V/--version rows. A name the user
 * claimed for an own or reachable global option is left out, exactly
 * as parse.c then treats it as the user's option.
 */
static void auto_rows(struct help *h, const arg_config *cfg)
{
	bool hs = false, hl = false, vs = false, vl = false, own;
	struct opt_walk ow;
	const arg_item *x;

	walk_init(&ow, h->pb);
	while ((x = walk_next(&ow, &own))) {
		hs = hs || x->short_name == 'h';
		vs = vs || x->short_name == 'V';
		hl = hl || is_named(x, "help");
		vl = vl || is_named(x, "version");
	}
	memset(&h->auto_help, 0, sizeof(h->auto_help));
	memset(&h->auto_version, 0, sizeof(h->auto_version));
	if (!(cfg->style & ARG_NO_AUTO_HELP) && !(hs && hl)) {
		h->auto_help.kind = ARG_I_FLAG;
		h->auto_help.short_name = hs ? 0 : 'h';
		h->auto_help.name = hl ? NULL : "help";
		h->auto_help.help = arg__msg(h->m, ARG_MSG_HELP_FLAG);
	}
	if (cfg->version_text && !(vs && vl)) {
		h->auto_version.kind = ARG_I_FLAG;
		h->auto_version.short_name = vs ? 0 : 'V';
		h->auto_version.name = vl ? NULL : "version";
		h->auto_version.help = arg__msg(h->m, ARG_MSG_VERSION_FLAG);
	}
}

/* ------------------------------------------------------------------ */
/* Cells                                                              */
/* ------------------------------------------------------------------ */

static void put(arg_write_fn w, void *ud, const char *s)
{
	if (s)
		w(ud, s, strlen(s));
}

static void put_metavar(arg_write_fn w, void *ud, const arg_item *it)
{
	const arg_type *t = arg__value_type(it);
	const char *mv = "VALUE";

	if (it->value)
		mv = it->value;
	else if (t && t->name)
		mv = t->name;
	put(w, ud, it->optional_value ? "[=<" : " <");
	put(w, ud, mv);
	put(w, ud, it->optional_value ? ">]" : ">");
}

/* "-o, --output <FILE>", "    --[no-]color", "[NAME]...", "build" */
static void put_cell(arg_write_fn w, void *ud, const arg_item *it)
{
	char sh[2] = { '-', 0 };

	if (it->kind == ARG_I_ARG) {
		bool req = it->min_count > 0 || it->required;

		put(w, ud, req ? "<" : "[");
		put(w, ud, it->name);
		put(w, ud, req ? ">" : "]");
		if (it->many)
			put(w, ud, "...");
		return;
	}
	if (!is_option(it)) {
		put(w, ud, it->name);
		return;
	}
	if (it->short_name) {
		sh[1] = it->short_name;
		w(ud, sh, sizeof(sh));
		if (it->name || it->alias)
			put(w, ud, ", ");
	} else {
		put(w, ud, "    ");
	}
	if (it->name) {
		put(w, ud, it->negatable ? "--[no-]" : "--");
		put(w, ud, it->name);
		if (it->alias)
			put(w, ud, ", ");
	}
	if (it->alias) {
		put(w, ud, "--");
		put(w, ud, it->alias);
	}
	if (it->kind == ARG_I_VALUE || (it->kind == ARG_I_ON && it->value))
		put_metavar(w, ud, it);
}

static size_t cell_cols(const arg_item *it)
{
	size_t n = 0;

	put_cell(meter_write, &n, it);
	return n;
}

/* ------------------------------------------------------------------ */
/* Rows                                                               */
/* ------------------------------------------------------------------ */

/* "(1-256)", "[default: 4]", "[env: X]", "[possible: a, b]" */
static void annotations(struct help *h, struct wrap *wr, const arg_item *it)
{
	const char *const *c = choices_of(it);
	const char *val[2];
	arg_msg_id tag[2];
	size_t i;

	if (it->min || it->max) {
		wrap_unit_begin(wr);
		wrap_str(wr, "(");
		if (!it->max)
			wrap_str(wr, ">= ");
		else if (!it->min)
			wrap_str(wr, "<= ");
		wrap_str(wr, it->min);
		if (it->min && it->max)
			wrap_str(wr, "-");
		wrap_str(wr, it->max);
		wrap_str(wr, ")");
		wrap_unit_end(wr);
	}
	val[0] = it->def;
	tag[0] = ARG_MSG_DEFAULT;
	val[1] = it->env;
	tag[1] = ARG_MSG_ENV;
	for (i = 0; i < 2; i++) {
		if (!val[i])
			continue;
		wrap_unit_begin(wr);
		wrap_str(wr, "[");
		wrap_str(wr, arg__msg(h->m, tag[i]));
		wrap_str(wr, ": ");
		wrap_str(wr, val[i]);
		wrap_str(wr, "]");
		wrap_unit_end(wr);
	}
	if (c && c[0]) {
		wrap_unit_begin(wr);
		wrap_str(wr, "[");
		wrap_str(wr, arg__msg(h->m, ARG_MSG_POSSIBLE));
		wrap_str(wr, ": ");
		for (i = 0; c[i]; i++) {
			if (i)
				wrap_str(wr, ", ");
			wrap_str(wr, c[i]);
		}
		wrap_str(wr, "]");
		wrap_unit_end(wr);
	}
}

/* "  <cell>   <description, wrapped at col>\n" */
static void row(struct help *h, const arg_item *it, size_t col)
{
	struct wrap wr;

	out_spaces(&h->o, HELP_INDENT);
	put_cell(out_write, &h->o, it);
	wrap_init(&wr, &h->o, col, HELP_INDENT + cell_cols(it), h->limit);
	wrap_str(&wr, it->help);
	if (it->kind != ARG_I_CMD)
		annotations(h, &wr, it);
	wrap_word_end(&wr);
	out_char(&h->o, '\n');
}

/* Column for cells of up to longest columns. */
static size_t column(const struct help *h, size_t longest, size_t gap)
{
	size_t col = HELP_INDENT + longest + gap;

	return col > h->width / 2 ? h->width / 2 : col;
}

/* ------------------------------------------------------------------ */
/* Sections                                                           */
/* ------------------------------------------------------------------ */

static void heading(struct help *h, const char *title, bool colon)
{
	out_char(&h->o, '\n');
	out_str(&h->o, title);
	if (colon)
		out_char(&h->o, ':');
	out_char(&h->o, '\n');
}

static bool shown(const arg_item *it)
{
	return it->kind != ARG_I_END;
}

static void usage(struct help *h, bool any_option)
{
	const arg_item *cmd = h->cmd;
	struct arg__iter it;
	const arg_item *x;
	bool subs = false;

	out_str(&h->o, arg__msg(h->m, ARG_MSG_USAGE));
	out_char(&h->o, ' ');
	arg__write_path(out_write, &h->o, &h->pb->path);
	if (any_option)
		out_str(&h->o, " [OPTIONS]");
	arg__iter_init(&it, cmd);
	while ((x = arg__iter_next(&it))) {
		if (x->kind == ARG_I_ARG && !x->hidden) {
			out_char(&h->o, ' ');
			put_cell(out_write, &h->o, x);
		}
		subs = subs || (x->kind == ARG_I_CMD && !x->hidden);
	}
	if (subs)
		out_str(&h->o, has_handler(cmd) ? " [COMMAND]" : " <COMMAND>");
	out_char(&h->o, '\n');
}

static void summary(struct help *h)
{
	struct wrap wr;

	if (!h->cmd->help || !h->cmd->help[0])
		return;
	out_char(&h->o, '\n');
	wrap_init(&wr, &h->o, 0, 0, h->limit);
	wrap_str(&wr, h->cmd->help);
	wrap_word_end(&wr);
	out_char(&h->o, '\n');
}

static void arguments(struct help *h)
{
	struct arg__iter it;
	const arg_item *x;
	bool any = false;

	arg__iter_init(&it, h->cmd);
	while ((x = arg__iter_next(&it))) {
		if (x->kind != ARG_I_ARG || x->hidden)
			continue;
		if (!any)
			heading(h, arg__msg(h->m, ARG_MSG_ARGUMENTS), false);
		any = true;
		row(h, x, h->col);
	}
}

/*
 * Rows of the options of one section: from the iterator position up
 * to the next ARG_GROUP. Returns false when nothing would be listed.
 */
static bool group_rows(struct help *h, struct arg__iter it, bool print)
{
	const arg_item *x;
	bool any = false;

	while ((x = arg__iter_next(&it)) && x->kind != ARG_I_GROUP) {
		if (!shown_option(x))
			continue;
		any = true;
		if (print)
			row(h, x, h->col);
	}
	return any;
}

static void options(struct help *h)
{
	struct opt_walk ow;
	struct arg__iter it;
	const arg_item *x;
	bool any, own;

	/* own ungrouped options, then globals, then the automatic rows */
	arg__iter_init(&it, h->cmd);
	any = group_rows(h, it, false) || shown(&h->auto_help) ||
	      shown(&h->auto_version);
	walk_init(&ow, h->pb);
	while (!any && (x = walk_next(&ow, &own)))
		any = !own && shown_option(x);
	if (!any)
		return;
	heading(h, arg__msg(h->m, ARG_MSG_OPTIONS), false);
	group_rows(h, it, true);
	walk_init(&ow, h->pb);
	while ((x = walk_next(&ow, &own)))
		if (!own && shown_option(x))
			row(h, x, h->col);
	if (shown(&h->auto_help))
		row(h, &h->auto_help, h->col);
	if (shown(&h->auto_version))
		row(h, &h->auto_version, h->col);
}

static void groups(struct help *h)
{
	struct arg__iter it;
	const arg_item *x;

	arg__iter_init(&it, h->cmd);
	while ((x = arg__iter_next(&it))) {
		if (x->kind != ARG_I_GROUP || !group_rows(h, it, false))
			continue;
		heading(h, x->help, true);
		group_rows(h, it, true);
	}
}

static void commands(struct help *h)
{
	struct arg__iter it;
	const arg_item *x;
	bool any = false;

	arg__iter_init(&it, h->cmd);
	while ((x = arg__iter_next(&it))) {
		if (x->kind != ARG_I_CMD || x->hidden)
			continue;
		if (!any)
			heading(h, arg__msg(h->m, ARG_MSG_COMMANDS), false);
		any = true;
		row(h, x, h->cmd_col);
	}
}

static void examples(struct help *h)
{
	struct arg__iter it;
	const arg_item *x;
	struct wrap wr;
	bool any = false;

	arg__iter_init(&it, h->cmd);
	while ((x = arg__iter_next(&it))) {
		if (x->kind != ARG_I_EXAMPLE)
			continue;
		if (!any)
			heading(h, arg__msg(h->m, ARG_MSG_EXAMPLES), false);
		any = true;
		out_spaces(&h->o, HELP_INDENT);
		out_str(&h->o, x->name);
		wrap_init(&wr, &h->o, 2 * HELP_INDENT,
			  HELP_INDENT + (x->name ? cols_of(x->name,
							   strlen(x->name))
						 : 0),
			  h->limit);
		wrap_str(&wr, x->help);
		wrap_word_end(&wr);
		out_char(&h->o, '\n');
	}
}

/* "See 'prog repo <command> --help' for more." when there are commands */
static void see_more(struct help *h)
{
	const char *s = arg__msg(h->m, ARG_MSG_SEE_COMMAND_HELP);
	const char *run = s;
	struct arg__iter it;
	const arg_item *x;
	bool any = false;

	arg__iter_init(&it, h->cmd);
	while ((x = arg__iter_next(&it)))
		any = any || (x->kind == ARG_I_CMD && !x->hidden);
	if (!any)
		return;
	out_char(&h->o, '\n');
	for (; *s; s++) {
		if (s[0] != '%' || (s[1] != 's' && s[1] != '%'))
			continue;
		/* "%%" keeps its first '%' in the run */
		out_write(&h->o, run, (size_t)(s - run) + (s[1] == '%'));
		if (s[1] == 's')
			arg__write_path(out_write, &h->o, &h->pb->path);
		run = ++s + 1;
	}
	out_str(&h->o, run);
	out_char(&h->o, '\n');
}

/* ------------------------------------------------------------------ */
/* Width and layout                                                   */
/* ------------------------------------------------------------------ */

/* COLUMNS as a plain decimal number in 20..1000, else 0. */
static size_t env_columns(const arg_config *cfg)
{
	const char *s;
	size_t n = 0;

	if (!cfg->getenv)
		return 0;
	s = cfg->getenv(cfg->env_ud, "COLUMNS");
	if (!s || !*s)
		return 0;
	for (; *s; s++) {
		if (*s < '0' || *s > '9' || n > 1000)
			return 0;
		n = n * 10 + (size_t)(*s - '0');
	}
	return n >= 20 && n <= 1000 ? n : 0;
}

static size_t help_width(const arg_config *cfg)
{
	size_t w = cfg->help_width;

	if (!w && cfg->term_width)
		w = cfg->term_width(cfg->width_ud);
	if (!w)
		w = env_columns(cfg);
	return w ? w : HELP_WIDTH;
}

static void keep_max(size_t *m, const arg_item *it)
{
	size_t n = cell_cols(it);

	if (n > *m)
		*m = n;
}

/* Description columns; returns whether any option is listed. */
static bool layout(struct help *h)
{
	size_t opt = 0, cmd = 0;
	struct opt_walk ow;
	struct arg__iter it;
	const arg_item *x;
	bool own, any = false;

	arg__iter_init(&it, h->cmd);
	while ((x = arg__iter_next(&it))) {
		if (x->hidden)
			continue;
		if (x->kind == ARG_I_ARG)
			keep_max(&opt, x);
		else if (x->kind == ARG_I_CMD)
			keep_max(&cmd, x);
	}
	walk_init(&ow, h->pb);
	while ((x = walk_next(&ow, &own))) {
		if (shown_option(x)) {
			keep_max(&opt, x);
			any = true;
		}
	}
	if (shown(&h->auto_help))
		keep_max(&opt, &h->auto_help);
	if (shown(&h->auto_version))
		keep_max(&opt, &h->auto_version);
	h->col = column(h, opt, HELP_GAP);
	h->cmd_col = column(h, cmd, HELP_CMD_GAP);
	return any || shown(&h->auto_help) || shown(&h->auto_version);
}

int arg__render_help(arg *p, const arg_item *cmd, arg_write_fn w, void *ud)
{
	const arg_config *cfg = &arg__cpriv(p)->cfg;
	struct arg__pathbuf pb;
	struct help h;
	bool any_option;

	if (!cmd || !w || !arg__cmd_path(p, cmd, &pb))
		return -1;
	h.o.w = w;
	h.o.ud = ud;
	h.o.err = 0;
	h.o.n = 0;
	h.m = cfg->messages;
	h.pb = &pb;
	h.cmd = cmd;
	h.width = help_width(cfg);
	h.limit = h.width > 1 ? h.width - 1 : 1;
	auto_rows(&h, cfg);
	any_option = layout(&h);

	usage(&h, any_option);
	summary(&h);
	arguments(&h);
	options(&h);
	groups(&h);
	commands(&h);
	examples(&h);
	see_more(&h);
	out_flush(&h.o);
	return h.o.err;
}

int arg_help(arg *p, const char *path, arg_write_fn write, void *ud)
{
	const struct arg__priv *v = arg__priv(p);
	struct arg__pathbuf pb;
	const arg_item *cmd = arg__find_cmd(p, path, &pb);

	if (!cmd)
		return -1;
	if (!write) {
		write = v->cfg.write_out;
		ud = v->cfg.io_ud;
	}
	if (v->cfg.render_help)
		return v->cfg.render_help(p, cmd, write, ud);
	return arg__render_help(p, cmd, write, ud);
}
