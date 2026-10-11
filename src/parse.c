// SPDX-License-Identifier: GPL-2.0-only
/*
 * parse.c - the main pass, value fallbacks, checks, report and run
 *           (SDD 6.5, 6.7, 7.2, 7.4, 9, 10)
 *
 * arg_parse() pulls tokens from the Level 2 tokenizer and matches them
 * against the index of the current command (spec.c), then its
 * ancestors' .global options. Positionals either descend into a
 * subcommand or fill the next positional slot. After the last token
 * unset items fall back to the value sources, the environment and
 * their default (in that order), .to bindings are written and the
 * checks run. The first error stops everything.
 */
#include "arg_internal.h"

#include <float.h>
#include <limits.h>

/* The private state must fit the public reserve (SDD 18 rule 3). */
typedef char arg__priv_fits[sizeof(struct arg__priv) <= ARG__PRIV_BYTES ?
			    1 : -1];

struct arg__priv *arg__priv(arg *p)
{
	return (struct arg__priv *)(void *)&p->_p;
}

const struct arg__priv *arg__cpriv(const arg *p)
{
	return (const struct arg__priv *)(const void *)&p->_p;
}

/* ------------------------------------------------------------------ */
/* Setup                                                              */
/* ------------------------------------------------------------------ */

#ifdef ARG_FREESTANDING
static int null_write(void *ud, const char *s, size_t n)
{
	(void)ud;
	(void)s;
	(void)n;
	return 0;
}
#define DEFAULT_OUT null_write
#define DEFAULT_ERR null_write
#else
#define DEFAULT_OUT arg__write_stdout
#define DEFAULT_ERR arg__write_stderr
#endif

/*
 * Only ARG_CONFIG_VERSION 1 exists, so every version reads the whole
 * struct; a later version will copy just the fields it knows.
 */
static void config_defaults(arg_config *c)
{
	if (!c->write_out)
		c->write_out = DEFAULT_OUT;
	if (!c->write_err)
		c->write_err = DEFAULT_ERR;
#if defined(ARG_NO_ENV)
	c->getenv = NULL;
#elif !defined(ARG_FREESTANDING)
	if (!c->getenv)
		c->getenv = arg__getenv_default;
#endif
#ifndef ARG_FREESTANDING
	if (!c->exit)
		c->exit = arg__exit_default;
#endif
	if (!c->rule_sources)
		c->rule_sources = ARG_RULE_SOURCES_DEFAULT;
}

void arg__init(arg *p, const arg_item *items, const arg_config *cfg,
	       int exit_on_error)
{
	struct arg__priv *v = arg__priv(p);

	memset(v, 0, sizeof(*v));
	if (cfg)
		v->cfg = *cfg;
	else
		v->cfg.version = ARG_CONFIG_VERSION;
	config_defaults(&v->cfg);
	v->root = items;
	v->exit_on_error = exit_on_error;
	v->unknown_argi = -1;
	v->err.argi = -1;
	arg__arena_reset(p);
}

void arg_free(arg *p)
{
	struct arg__priv *v = arg__priv(p);

	arg__arena_reset(p);
	v->depth = 0;
	v->rest = NULL;
	v->path_str = NULL;
}

/* ------------------------------------------------------------------ */
/* Errors                                                             */
/* ------------------------------------------------------------------ */

arg_status arg__set_error(arg *p, arg_status st, const arg_item *item,
			  const char *detail)
{
	struct arg__priv *v = arg__priv(p);
	arg_error *e = &v->err;

	memset(e, 0, sizeof(*e));
	e->status = st;
	e->argi = -1;
	e->cmd = v->depth ? v->cmds[v->depth - 1] : v->root;
	e->item = item;
	e->detail = detail;
	return st;
}

const char *arg__take_fail(arg *p)
{
	struct arg__priv *v = arg__priv(p);
	const char *m = v->fail_msg;

	v->fail_msg = NULL;
	return m;
}

void arg__selected_path(const arg *p, struct arg__path *out)
{
	const struct arg__priv *v = arg__cpriv(p);

	out->prog = v->prog;
	out->cmds = v->depth ? v->cmds : &v->root;
	out->depth = v->depth ? v->depth : 1;
}

const arg_error *arg_error_get(const arg *p)
{
	return &arg__cpriv(p)->err;
}

int arg_fail(arg *a, const char *msg)
{
	arg__priv(a)->fail_msg = msg;
	return 1;
}

/* ------------------------------------------------------------------ */
/* Main pass                                                          */
/* ------------------------------------------------------------------ */

struct ps {
	arg *p;
	struct arg__priv *v;
	arg_tokenizer tz;
	struct arg__cmd *cur; /* deepest command entered so far */
	uint32_t style;       /* tokenizer style without NEGATIVE_NUMBERS */
	bool done;            /* on_unknown_cmd claimed the rest         */
};

static bool is_word(const char *s, size_t n, const char *w)
{
	return strlen(w) == n && memcmp(s, w, n) == 0;
}

static bool auto_help(const struct ps *ps)
{
	return !(ps->v->cfg.style & ARG_NO_AUTO_HELP);
}

static bool auto_version(const struct ps *ps)
{
	return ps->v->cfg.version_text != NULL;
}

static void seen(struct arg__state *s)
{
	if (s->count < UINT32_MAX)
		s->count++;
	s->source = ARG_SRC_CLI;
}

/* Error about the option of token t; text is "-x" or "--name". */
static arg_status opt_error(struct ps *ps, arg_status st, const arg_item *it,
			    const arg_tok *t)
{
	struct arg__priv *v = ps->v;

	arg__set_error(ps->p, st, it, NULL);
	v->err.argi = t->argi;
	v->err.offset = t->offset;
	if (t->kind == ARG_T_SHORT) {
		v->short_text[0] = '-';
		v->short_text[1] = t->ch;
		v->short_text[2] = '\0';
		v->err.text = v->short_text;
		v->err.text_len = 2;
	} else {
		v->err.text = v->argv[t->argi];
		v->err.text_len = 2 + t->name_len;
	}
	return st;
}

/* Lookup in the current command, then .global options of ancestors. */
static struct arg__state *chain_short(struct arg__cmd *cur, char ch)
{
	struct arg__cmd *c;

	for (c = cur; c; c = c->parent) {
		struct arg__state *s = arg__short_lookup(c, ch);

		if (s && (c == cur || s->item->global))
			return s;
	}
	return NULL;
}

static struct arg__state *chain_long(struct arg__cmd *cur, const char *name,
				     size_t n, bool *neg)
{
	struct arg__cmd *c;

	for (c = cur; c; c = c->parent) {
		struct arg__state *s = arg__long_lookup(c, name, n, neg);

		if (s && (c == cur || s->item->global))
			return s;
	}
	return NULL;
}

/* Rule 4: "-5" is a positional unless a reachable option is a digit. */
static void set_style(struct ps *ps)
{
	bool digit = ps->cur->digit_short;
	struct arg__cmd *c;

	for (c = ps->cur->parent; c && !digit; c = c->parent)
		digit = c->digit_global;
	ps->tz.style = ps->style | (digit ? 0u : ARG_TOK_NEGATIVE_NUMBERS);
}

/* ARG_STRICT_VALUES: does s name an option known here? */
static bool known_option(struct ps *ps, const char *s)
{
	size_t n;

	if (s[0] != '-' || !s[1])
		return false;
	if (s[1] == '-') {
		if (!s[2])
			return true;
		s += 2;
		n = strcspn(s, "=");
		return chain_long(ps->cur, s, n, NULL) ||
		       (auto_help(ps) && is_word(s, n, "help")) ||
		       (auto_version(ps) && is_word(s, n, "version"));
	}
	if ((ps->tz.style & ARG_TOK_NEGATIVE_NUMBERS) && s[1] >= '0' &&
	    s[1] <= '9')
		return false;
	return chain_short(ps->cur, s[1]) || (auto_help(ps) && s[1] == 'h') ||
	       (auto_version(ps) && s[1] == 'V');
}

/*
 * The value of an option (rules 3, 5, 6, 7). *out stays NULL for an
 * absent optional value.
 */
static arg_status get_value(struct ps *ps, const arg_item *it,
			    const arg_tok *t, const char **out)
{
	int before = ps->tz.argi;

	*out = NULL;
	if (t->kind == ARG_T_LONG && t->value) {
		*out = t->value;
		return ARG_OK;
	}
	if (it->optional_value) {
		if (t->kind == ARG_T_SHORT)
			*out = arg_tok_take_attached(&ps->tz);
		return ARG_OK;
	}
	*out = arg_tok_take_value(&ps->tz);
	if (*out && ps->tz.argi != before &&
	    (ps->v->cfg.style & ARG_STRICT_VALUES) && known_option(ps, *out))
		*out = NULL;
	if (!*out)
		return opt_error(ps, ARG_E_MISSING_VALUE, it, t);
	return ARG_OK;
}

/* Store a value that lies in argv[tz.argi - 1]. */
static arg_status cli_store(struct ps *ps, struct arg__state *s,
			    const char *val)
{
	int argi = ps->tz.argi - 1;
	arg_status st = arg__store_text(ps->p, s, val, strlen(val), argi);

	if (st != ARG_OK && ps->v->err.argi == argi)
		ps->v->err.offset = (size_t)(val - ps->v->argv[argi]);
	return st;
}

static arg_status opt_flag(struct ps *ps, struct arg__state *s,
			   const arg_tok *t, bool neg)
{
	bool b = !neg;
	arg_status st;

	if (t->kind == ARG_T_LONG && t->value) {
		/* rule 8: --flag=yes; --no-flag=... makes no sense */
		if (neg)
			return opt_error(ps, ARG_E_UNEXPECTED_VALUE, s->item,
					 t);
		st = cli_store(ps, s, t->value);
		if (st != ARG_OK)
			return st;
	} else {
		if (!s->v.one)
			s->v.one = arg__alloc(ps->p, sizeof(bool));
		if (!s->v.one)
			return arg__set_error(ps->p, ARG_E_NO_MEMORY, s->item,
					      NULL);
		memcpy(s->v.one, &b, sizeof(b));
	}
	seen(s);
	return ARG_OK;
}

static arg_status opt_value(struct ps *ps, struct arg__state *s,
			    const arg_tok *t)
{
	const arg_item *it = s->item;
	const char *val;
	arg_status st;

	st = get_value(ps, it, t, &val);
	if (st != ARG_OK)
		return st;
	seen(s);
	if (val)
		return cli_store(ps, s, val);
	/* --color without a value: the default, if there is one */
	if (it->def)
		return arg__store_text(ps->p, s, it->def, strlen(it->def),
				       t->argi);
	return ARG_OK;
}

/* ARG_ON: runs now, sees the values given before it (SDD 6.5). */
static arg_status opt_on(struct ps *ps, struct arg__state *s,
			 const arg_tok *t)
{
	const arg_item *it = s->item;
	const char *val = NULL;
	arg_status st;

	if (it->value || it->optional_value) {
		st = get_value(ps, it, t, &val);
		if (st != ARG_OK)
			return st;
	} else if (t->kind == ARG_T_LONG && t->value) {
		return opt_error(ps, ARG_E_UNEXPECTED_VALUE, it, t);
	}
	seen(s);
	ps->v->fail_msg = NULL;
	if (it->fn.on_each(ps->p, val) == 0)
		return ARG_OK;
	arg__set_error(ps->p, ARG_E_ACTION, it, arg__take_fail(ps->p));
	ps->v->err.argi = t->argi;
	if (val) {
		ps->v->err.text = val;
		ps->v->err.text_len = strlen(val);
	}
	return ARG_E_ACTION;
}

static arg_status on_option(struct ps *ps, struct arg__state *s,
			    const arg_tok *t, bool neg)
{
	const arg_item *it = s->item;

	if (it->once && s->source == ARG_SRC_CLI)
		return opt_error(ps, ARG_E_REPEATED, it, t);
	switch (it->kind) {
	case ARG_I_FLAG:
		return opt_flag(ps, s, t, neg);
	case ARG_I_VALUE:
		return opt_value(ps, s, t);
	case ARG_I_ON:
		return opt_on(ps, s, t);
	default:
		break;
	}
	/* COUNT and DO */
	if (t->kind == ARG_T_LONG && t->value)
		return opt_error(ps, ARG_E_UNEXPECTED_VALUE, it, t);
	if (it->kind == ARG_I_DO && !ps->v->do_item)
		ps->v->do_item = it;
	seen(s);
	return ARG_OK;
}

static arg_status want(struct ps *ps, arg_status st)
{
	return arg__set_error(ps->p, st, NULL, NULL);
}

static arg_status on_short(struct ps *ps, const arg_tok *t)
{
	struct arg__state *s = chain_short(ps->cur, t->ch);
	char *d;

	if (s)
		return on_option(ps, s, t, false);
	if (auto_help(ps) && t->ch == 'h')
		return want(ps, ARG_EXIT_HELP);
	if (auto_version(ps) && t->ch == 'V')
		return want(ps, ARG_EXIT_VERSION);
	if (!(ps->v->cfg.style & ARG_ALLOW_UNKNOWN))
		return opt_error(ps, ARG_E_UNKNOWN_OPTION, NULL, t);
	if (t->offset == 1) {
		/* pass the whole argument on, bundle included */
		ps->tz.pos = 0;
		return arg__rest_push(ps->p, ps->v->argv[t->argi]);
	}
	d = arg__alloc(ps->p, 3);
	if (!d)
		return arg__set_error(ps->p, ARG_E_NO_MEMORY, NULL, NULL);
	d[0] = '-';
	d[1] = t->ch;
	d[2] = '\0';
	return arg__rest_push(ps->p, d);
}

static void suggest_long(struct ps *ps, const arg_tok *t)
{
	struct arg__suggester sg;
	struct arg__cmd *c;
	size_t i;

	arg__suggest_init(&sg, t->name, t->name_len, ps->v->cfg.suggest);
	for (c = ps->cur; c; c = c->parent) {
		for (i = 0; i < c->nopt; i++) {
			const arg_item *it = c->st[i].item;

			if (c != ps->cur && !it->global)
				continue;
			arg__suggest_add(&sg, it->name);
			arg__suggest_add(&sg, it->alias);
		}
	}
	if (auto_help(ps))
		arg__suggest_add(&sg, "help");
	if (auto_version(ps))
		arg__suggest_add(&sg, "version");
	ps->v->err.suggestion = arg__suggest_best(&sg);
	ps->v->err.suggestion_prefix = "--";
}

static arg_status on_long(struct ps *ps, const arg_tok *t)
{
	bool neg = false;
	struct arg__state *s = chain_long(ps->cur, t->name, t->name_len, &neg);

	if (s)
		return on_option(ps, s, t, neg);
	if (auto_help(ps) && is_word(t->name, t->name_len, "help"))
		return want(ps, ARG_EXIT_HELP);
	if (auto_version(ps) && is_word(t->name, t->name_len, "version"))
		return want(ps, ARG_EXIT_VERSION);
	if (ps->v->cfg.style & ARG_ALLOW_UNKNOWN)
		return arg__rest_push(ps->p, ps->v->argv[t->argi]);
	opt_error(ps, ARG_E_UNKNOWN_OPTION, NULL, t);
	suggest_long(ps, t);
	return ARG_E_UNKNOWN_OPTION;
}

static arg_status descend(struct ps *ps, const arg_item *sub)
{
	struct arg__priv *v = ps->v;
	struct arg__cmd *c;
	arg_status st;

	st = arg__index(ps->p, sub, ps->cur, &c);
	if (st != ARG_OK)
		return st;
	v->cmds[v->depth] = sub;
	v->idx[v->depth] = c;
	v->depth++;
	v->path_str = NULL;
	ps->cur = c;
	set_style(ps);
	arg_tok_resume_options(&ps->tz);
	return ARG_OK;
}

static arg_status unknown_cmd_error(struct ps *ps, const arg_tok *t)
{
	struct arg__suggester sg;
	struct arg__iter it;
	const arg_item *x;
	arg_error *e = &ps->v->err;

	arg__set_error(ps->p, ARG_E_UNKNOWN_COMMAND, NULL, NULL);
	e->argi = t->argi;
	e->text = t->value;
	e->text_len = strlen(t->value);
	arg__suggest_init(&sg, e->text, e->text_len, ps->v->cfg.suggest);
	arg__iter_init(&it, ps->cur->cmd);
	while ((x = arg__iter_next(&it))) {
		if (x->kind == ARG_I_CMD)
			arg__suggest_add(&sg, x->name);
	}
	if (ps->v->cfg.style & ARG_HELP_COMMAND)
		arg__suggest_add(&sg, "help");
	e->suggestion = arg__suggest_best(&sg);
	e->suggestion_prefix = "";
	return ARG_E_UNKNOWN_COMMAND;
}

/* "prog help repo add": help for the named command (ARG_HELP_COMMAND). */
static arg_status help_command(struct ps *ps)
{
	const arg_item *sub;
	arg_status st;
	arg_tok t;

	while (arg_tok_next(&ps->tz, &t) && t.kind == ARG_T_POSITIONAL) {
		sub = arg__sub_lookup(ps->cur->cmd, t.value, strlen(t.value));
		if (!sub)
			return unknown_cmd_error(ps, &t);
		st = descend(ps, sub);
		if (st != ARG_OK)
			return st;
	}
	return want(ps, ARG_EXIT_HELP);
}

static arg_status fill_positional(struct ps *ps, const arg_tok *t)
{
	struct arg__cmd *c = ps->cur;
	arg_status st;

	while (c->pos_next < c->npos) {
		struct arg__state *s = &c->st[c->nopt + c->pos_next];
		const arg_item *it = s->item;

		if (it->many && it->max_count &&
		    arg__state_len(s) >= it->max_count) {
			c->pos_next++;
			continue;
		}
		st = cli_store(ps, s, t->value);
		if (st != ARG_OK)
			return st;
		seen(s);
		c->pos_given++;
		if (!it->many)
			c->pos_next++;
		return ARG_OK;
	}
	arg__set_error(ps->p, ARG_E_TOO_MANY_ARGS, NULL, NULL);
	ps->v->err.argi = t->argi;
	ps->v->err.text = t->value;
	ps->v->err.text_len = strlen(t->value);
	return ARG_E_TOO_MANY_ARGS;
}

/* SDD 7.4 step 3. */
static arg_status on_positional(struct ps *ps, const arg_tok *t)
{
	struct arg__cmd *c = ps->cur;
	const arg_item *sub;

	if (c->nsub && c->pos_given == 0) {
		sub = arg__sub_lookup(c->cmd, t->value, strlen(t->value));
		if (sub)
			return descend(ps, sub);
		if ((ps->v->cfg.style & ARG_HELP_COMMAND) &&
		    strcmp(t->value, "help") == 0)
			return help_command(ps);
		if (c->npos == 0 && ps->v->cfg.on_unknown_cmd) {
			ps->v->unknown_argi = t->argi;
			ps->done = true;
			return ARG_OK;
		}
		if (c->npos == 0)
			return unknown_cmd_error(ps, t);
	}
	return fill_positional(ps, t);
}

static arg_status main_loop(struct ps *ps)
{
	arg_status st;
	arg_tok t;

	while (!ps->done && arg_tok_next(&ps->tz, &t)) {
		switch (t.kind) {
		case ARG_T_SHORT:
			st = on_short(ps, &t);
			break;
		case ARG_T_LONG:
			st = on_long(ps, &t);
			break;
		case ARG_T_POSITIONAL:
			st = on_positional(ps, &t);
			break;
		default: /* "--" */
			st = ARG_OK;
			break;
		}
		if (st != ARG_OK)
			return st;
	}
	return ARG_OK;
}

/* ------------------------------------------------------------------ */
/* After the pass: fallbacks and bindings                             */
/* ------------------------------------------------------------------ */

/* Rule 15: extra sources (in order) > env > default. */
static arg_status fallback(arg *p, const arg_item *cmd, struct arg__state *s)
{
	struct arg__priv *v = arg__priv(p);
	const arg_item *it = s->item;
	const char *text = NULL, *key;
	uint8_t src = ARG_SRC_NONE;
	arg_status st;
	char kb[3];
	size_t i;

	if (s->source != ARG_SRC_NONE || !arg__value_type(it))
		return ARG_OK;
	key = arg__item_key(it, kb);
	for (i = 0; !text && i < ARG_MAX_SOURCES; i++) {
		if (v->cfg.sources[i]) {
			text = v->cfg.sources[i](v->cfg.src_ud, cmd, it, key);
			src = ARG_SRC_SOURCE;
		}
	}
	if (!text && it->env && v->cfg.getenv) {
		text = v->cfg.getenv(v->cfg.env_ud, it->env);
		src = ARG_SRC_ENV;
	}
	if (!text && it->def) {
		text = it->def;
		src = ARG_SRC_DEFAULT;
	}
	if (!text)
		return ARG_OK;
	st = arg__store_text(p, s, text, strlen(text), -1);
	if (st == ARG_OK)
		s->source = src;
	return st;
}

/* Text for an error about a stored value; NULL when it has none. */
static void value_text(arg *p, const arg_item *it, const void *src)
{
	const arg_type *t = arg__value_type(it);
	arg_error *e = &arg__priv(p)->err;
	const char *s = NULL;
	char *buf;
	int idx;

	if (!src)
		return;
	if (t->kind == ARG_K_STR) {
		memcpy(&s, src, sizeof(s));
	} else if (t->kind == ARG_K_ENUM) {
		memcpy(&idx, src, sizeof(idx));
		s = it->names[idx];
	} else if (t->format && (buf = arg__alloc(p, 64)) &&
		   t->format(t, src, buf, 64) >= 0) {
		s = buf;
	}
	e->text = s;
	e->text_len = s ? strlen(s) : 0;
}

static arg_status range_error(arg *p, const struct arg__state *s,
			      const void *src, const char *detail)
{
	arg__set_error(p, ARG_E_RANGE, s->item, detail);
	value_text(p, s->item, src);
	return ARG_E_RANGE;
}

#define BIND_RANGE "does not fit the bound variable"

/* An integer as sign and magnitude, so every target can be checked. */
struct num {
	bool neg;
	uint64_t mag;
};

static bool load_num(const arg_type *t, const void *src, struct num *n)
{
	int64_t i;
	uint64_t u;

	if (arg__load_uint(t, src, &u)) {
		n->neg = false;
		n->mag = u;
		return true;
	}
	if (!arg__load_int(t, src, &i))
		return false;
	n->neg = i < 0;
	n->mag = i < 0 ? (uint64_t)(-(i + 1)) + 1u : (uint64_t)i;
	return true;
}

static bool fits(const struct num *n, uint64_t neg_max, uint64_t pos_max)
{
	return n->mag <= (n->neg ? neg_max : pos_max);
}

/* Only called for values that fit int64_t. */
static int64_t sval(const struct num *n)
{
	if (!n->neg)
		return (int64_t)n->mag;
	if (n->mag > (uint64_t)INT64_MAX)
		return INT64_MIN;
	return -(int64_t)n->mag;
}

static arg_status bind_int(arg *p, const struct arg__state *s,
			   const arg_type *t, const void *src)
{
	void *ptr = s->item->to.ptr;
	struct num n;

	if (!load_num(t, src, &n))
		return ARG_OK; /* a custom type with an odd size */
	switch (s->item->to.type) {
	case ARG_TO_T_INT:
		if (!fits(&n, (uint64_t)INT_MAX + 1u, INT_MAX))
			break;
		*(int *)ptr = (int)sval(&n);
		return ARG_OK;
	case ARG_TO_T_LONG:
		if (!fits(&n, (uint64_t)LONG_MAX + 1u, LONG_MAX))
			break;
		*(long *)ptr = (long)sval(&n);
		return ARG_OK;
	case ARG_TO_T_UINT:
		if (!fits(&n, 0, UINT_MAX))
			break;
		*(unsigned *)ptr = (unsigned)n.mag;
		return ARG_OK;
	case ARG_TO_T_ULONG:
		if (!fits(&n, 0, ULONG_MAX))
			break;
		*(unsigned long *)ptr = (unsigned long)n.mag;
		return ARG_OK;
	case ARG_TO_T_U64:
		if (!fits(&n, 0, UINT64_MAX))
			break;
		*(uint64_t *)ptr = n.mag;
		return ARG_OK;
	case ARG_TO_T_SIZE:
		if (!fits(&n, 0, SIZE_MAX))
			break;
		*(size_t *)ptr = (size_t)n.mag;
		return ARG_OK;
	default: /* ARG_TO_T_I64 */
		if (!fits(&n, (uint64_t)INT64_MAX + 1u, INT64_MAX))
			break;
		*(int64_t *)ptr = sval(&n);
		return ARG_OK;
	}
	return range_error(p, s, src, BIND_RANGE);
}

static arg_status bind_float(arg *p, const struct arg__state *s,
			     const arg_type *t, const void *src)
{
	double d = 0;

	arg__load_double(t, src, &d);
	if (s->item->to.type == ARG_TO_T_DOUBLE) {
		*(double *)s->item->to.ptr = d;
		return ARG_OK;
	}
	if (d == d && (d > FLT_MAX || d < -FLT_MAX) && d - d == 0)
		return range_error(p, s, src, BIND_RANGE);
	*(float *)s->item->to.ptr = (float)d;
	return ARG_OK;
}

/* .to: the value, or the last one of a list. */
static arg_status bind(arg *p, const struct arg__state *s)
{
	const arg_item *it = s->item;
	const arg_type *t = arg__value_type(it);
	size_t n = arg__state_len(s);
	const void *src;

	if (it->to.type == ARG_TO_T_NONE || n == 0)
		return ARG_OK;
	src = arg__state_value(s, n - 1);
	switch (it->to.type) {
	case ARG_TO_T_RAW:
		memcpy(it->to.ptr, src, t->size);
		return ARG_OK;
	case ARG_TO_T_BOOL:
		memcpy(it->to.ptr, src, sizeof(bool));
		return ARG_OK;
	case ARG_TO_T_STR:
		memcpy(it->to.ptr, src, sizeof(const char *));
		return ARG_OK;
	case ARG_TO_T_DOUBLE:
	case ARG_TO_T_FLOAT:
		return bind_float(p, s, t, src);
	default:
		return bind_int(p, s, t, src);
	}
}

typedef arg_status (*state_fn)(arg *p, const arg_item *cmd,
			       struct arg__state *s);

/* Run fn over every option and positional on the selected path. */
static arg_status each_state(arg *p, state_fn fn)
{
	struct arg__priv *v = arg__priv(p);
	arg_status st;
	size_t d, i;

	for (d = 0; d < v->depth; d++) {
		struct arg__cmd *c = v->idx[d];

		for (i = 0; i < c->nopt + c->npos; i++) {
			st = fn(p, c->cmd, &c->st[i]);
			if (st != ARG_OK)
				return st;
		}
	}
	return ARG_OK;
}

static arg_status bind_fn(arg *p, const arg_item *cmd, struct arg__state *s)
{
	(void)cmd;
	return bind(p, s);
}

/* ------------------------------------------------------------------ */
/* Checks                                                             */
/* ------------------------------------------------------------------ */

static struct arg__cmd *deepest(arg *p)
{
	struct arg__priv *v = arg__priv(p);

	return v->idx[v->depth - 1];
}

static arg_status check_command(arg *p)
{
	struct arg__cmd *c = deepest(p);
	size_t i;

	if (c->nsub && !c->cmd->fn.run && !c->main)
		return arg__set_error(p, ARG_E_MISSING_COMMAND, NULL, NULL);
	for (i = c->nopt; i < c->nopt + c->npos; i++) {
		const struct arg__state *s = &c->st[i];

		if (arg__state_len(s) < s->item->min_count)
			return arg__set_error(p, ARG_E_TOO_FEW_ARGS, s->item,
					      NULL);
	}
	return ARG_OK;
}

static arg_status required_fn(arg *p, const arg_item *cmd,
			      struct arg__state *s)
{
	(void)cmd;
	if (s->item->required && s->item->kind != ARG_I_ARG &&
	    !arg__state_is_set(p, s))
		return arg__set_error(p, ARG_E_MISSING_REQUIRED, s->item,
				      NULL);
	return ARG_OK;
}

/* NUL-terminated arena copy of a, b, c joined; NULL without memory. */
static const char *cat3(arg *p, const char *a, const char *b, const char *c)
{
	size_t na = strlen(a), nb = strlen(b), nc = strlen(c);
	char *s = arg__alloc(p, na + nb + nc + 1);

	if (s) {
		memcpy(s, a, na);
		memcpy(s + na, b, nb);
		memcpy(s + na + nb, c, nc + 1);
	}
	return s;
}

static const char *bound_detail(arg *p, const arg_item *it)
{
	if (it->min && it->max) {
		const char *lo = cat3(p, "must be between ", it->min, " and ");

		return lo ? cat3(p, lo, it->max, "") : NULL;
	}
	if (it->min)
		return cat3(p, "must be at least ", it->min, "");
	return cat3(p, "must be at most ", it->max, "");
}

/* A bound parsed into arena memory; NULL if absent. */
static const void *bound(arg *p, const arg_item *it, const char *text)
{
	const arg_type *t = arg__value_type(it);
	arg_ctx ctx = { NULL, NULL, NULL, ARG_E_BAD_VALUE };
	void *b;

	if (!text)
		return NULL;
	b = arg__alloc(p, t->size);
	ctx.parser = p;
	ctx.item = it;
	if (!b || t->parse(t, text, strlen(text), b, &ctx) != 0)
		return NULL;
	return b;
}

static arg_status range_fn(arg *p, const arg_item *cmd, struct arg__state *s)
{
	const arg_item *it = s->item;
	const arg_type *t = arg__value_type(it);
	size_t i, n = arg__state_len(s);
	const void *lo, *hi;
	int cmp;

	(void)cmd;
	if ((!it->min && !it->max) || n == 0)
		return ARG_OK;
	lo = bound(p, it, it->min);
	hi = bound(p, it, it->max);
	for (i = 0; i < n; i++) {
		const void *x = arg__state_value(s, i);

		if ((lo && arg__compare(t, x, lo, &cmp) && cmp < 0) ||
		    (hi && arg__compare(t, x, hi, &cmp) && cmp > 0))
			return range_error(p, s, x, bound_detail(p, it));
	}
	return ARG_OK;
}

/* Per-item .check functions; only for items that have a value. */
static arg_status check_fn(arg *p, const arg_item *cmd, struct arg__state *s)
{
	const arg_item *it = s->item;
	char kb[3];

	(void)cmd;
	if (!it->check || s->source == ARG_SRC_NONE)
		return ARG_OK;
	arg__priv(p)->fail_msg = NULL;
	if (it->check(p, arg__item_key(it, kb)) == 0)
		return ARG_OK;
	arg__set_error(p, ARG_E_BAD_VALUE, it, arg__take_fail(p));
	if (arg__value_type(it) && it->kind != ARG_I_COUNT)
		value_text(p, it, arg__state_value(s, 0));
	return ARG_E_BAD_VALUE;
}

static arg_status run_checks(arg *p)
{
	arg_status st = check_command(p);

	if (st == ARG_OK)
		st = each_state(p, required_fn);
	if (st == ARG_OK)
		st = each_state(p, range_fn);
	if (st == ARG_OK)
		st = arg__rules_check(p);
	if (st == ARG_OK)
		st = each_state(p, check_fn);
	return st;
}

static arg_status finish(struct ps *ps)
{
	const arg_item *d = ps->v->do_item;
	arg_status st;

	st = each_state(ps->p, fallback);
	if (st == ARG_OK)
		st = each_state(ps->p, bind_fn);
	if (st != ARG_OK || ps->done || (d && !d->check_on_do))
		return st;
	return run_checks(ps->p);
}

/* ------------------------------------------------------------------ */
/* arg_parse                                                          */
/* ------------------------------------------------------------------ */

static void reset(arg *p, int argc, char *const *argv)
{
	struct arg__priv *v = arg__priv(p);
	const char *a0, *s;

	arg__arena_reset(p);
	arg__set_error(p, ARG_OK, NULL, NULL);
	v->status = ARG_OK;
	v->depth = 0;
	v->prog = NULL;
	v->do_item = NULL;
	v->fail_msg = NULL;
	v->rest = NULL;
	v->unknown_argi = -1;
	v->path_str = NULL;
	v->argv = argv;
	v->argc = 0;
	while (argv && v->argc < argc && argv[v->argc])
		v->argc++;
	if (v->argc == 0 || !argv[0][0])
		return;
	a0 = argv[0];
	for (s = a0; *s; s++) {
		if (*s == '/' && s[1])
			a0 = s + 1;
	}
	v->prog = a0;
}

static bool posix_mode(const struct arg__priv *v)
{
	if (v->cfg.style & ARG_STYLE_POSIX)
		return true;
	return (v->cfg.style & ARG_HONOR_POSIXLY_CORRECT) && v->cfg.getenv &&
	       v->cfg.getenv(v->cfg.env_ud, "POSIXLY_CORRECT");
}

static arg_status parse_core(arg *p, int argc, char *const *argv)
{
	struct arg__priv *v = arg__priv(p);
	struct ps ps;
	arg_status st;

	reset(p, argc, argv);
	if (!v->root || v->root->kind != ARG_I_CMD)
		return arg__set_error(p, ARG_E_SPEC, v->root,
				      "root must be an ARG_I_CMD item");
	v->cmds[0] = v->root;
	v->idx[0] = NULL;
	st = arg__index(p, v->root, NULL, &v->idx[0]);
	if (st != ARG_OK)
		return st;
	v->depth = 1;
	memset(&ps, 0, sizeof(ps));
	ps.p = p;
	ps.v = v;
	ps.cur = v->idx[0];
	ps.style = posix_mode(v) ? ARG_STYLE_POSIX : 0u;
	arg_tok_init(&ps.tz, v->argc, argv, ps.style);
	set_style(&ps);
	st = main_loop(&ps);
	if (st != ARG_OK)
		return st;
	return finish(&ps);
}

arg_status arg_parse(arg *p, int argc, char *const *argv)
{
	struct arg__priv *v = arg__priv(p);
	arg_status st = parse_core(p, argc, argv);
	int code;

	v->status = st;
	v->err.status = st;
	if (st != ARG_OK && v->exit_on_error) {
		code = arg_report(p);
		/* release alloc-hook blocks even if the exit hook returns */
		arg_free(p);
		if (v->cfg.exit)
			v->cfg.exit(v->cfg.exit_ud, code);
	}
	return st;
}

/* ------------------------------------------------------------------ */
/* Report and run                                                     */
/* ------------------------------------------------------------------ */

static const arg_item *selected(const struct arg__priv *v)
{
	return v->depth ? v->cmds[v->depth - 1] : v->root;
}

int arg_report(arg *p)
{
	struct arg__priv *v = arg__priv(p);
	const arg_config *c = &v->cfg;
	struct arg__path path;

	switch (v->status) {
	case ARG_OK:
		return 0;
	case ARG_EXIT_HELP:
		if (c->render_help)
			c->render_help(p, selected(v), c->write_out, c->io_ud);
		else
			arg__render_help(p, selected(v), c->write_out,
					 c->io_ud);
		return 0;
	case ARG_EXIT_VERSION:
		if (c->version_text &&
		    c->write_out(c->io_ud, c->version_text,
				 strlen(c->version_text)) == 0)
			c->write_out(c->io_ud, "\n", 1);
		return 0;
	default:
		break;
	}
	if (c->on_error)
		return c->on_error(p, &v->err);
	arg__selected_path(p, &path);
	arg__write_error(c->write_err, c->io_ud, &v->err, &path, c->messages);
	return arg_exit_code(v->status);
}

static void print_fail(arg *p, const char *msg)
{
	struct arg__priv *v = arg__priv(p);
	arg_write_fn w = v->cfg.write_err;
	void *ud = v->cfg.io_ud;
	struct arg__path path;

	arg__selected_path(p, &path);
	if (arg__write_path(w, ud, &path) == 0 && w(ud, ": ", 2) == 0 &&
	    w(ud, msg, strlen(msg)) == 0)
		w(ud, "\n", 1);
}

int arg_run(arg *p)
{
	struct arg__priv *v = arg__priv(p);
	arg_run_fn fn = NULL;
	const char *msg;
	int r;

	if (v->status != ARG_OK)
		return arg_exit_code(v->status);
	if (!v->depth)
		return 0;
	v->fail_msg = NULL;
	if (v->do_item) {
		r = v->do_item->fn.on_do(p);
	} else if (v->unknown_argi >= 0) {
		r = v->cfg.on_unknown_cmd(p, v->argc - v->unknown_argi,
					  v->argv + v->unknown_argi);
	} else {
		fn = selected(v)->fn.run;
		if (!fn && v->idx[v->depth - 1]->main)
			fn = v->idx[v->depth - 1]->main->fn.run;
		if (!fn)
			return 0;
		r = fn(p);
	}
	msg = arg__take_fail(p);
	if (r != 0 && msg)
		print_fail(p, msg);
	return r;
}
