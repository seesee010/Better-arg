// SPDX-License-Identifier: GPL-2.0-only
/*
 * spec.c - spec compiler, per-command index and tree walks (SDD 5, 9, 14)
 *
 * A command's index is built the first time the command is entered.
 * Building it is also where the spec is checked: every child item of
 * the command is validated, duplicate names and keys are rejected,
 * .def / .min / .max must parse with the item's type and a .to binding
 * must fit it. arg_compile() runs the same code over the whole tree.
 *
 * Lookup structures (SDD 14):
 *   - short options: a 128-entry byte table, index + 1 into st[];
 *   - long names, aliases and generated "no-" names: sorted by
 *     (length, bytes) with cached lengths, binary search.
 */
#include "arg_internal.h"

/* ------------------------------------------------------------------ */
/* Iterator                                                           */
/* ------------------------------------------------------------------ */

void arg__iter_init(struct arg__iter *it, const arg_item *cmd)
{
	it->sp = 0;
	it->error = NULL;
	it->error_item = NULL;
	if (cmd && cmd->items)
		it->stack[it->sp++] = cmd->items;
}

static void iter_bad(struct arg__iter *it, const arg_item *x, const char *e)
{
	if (!it->error) {
		it->error = e;
		it->error_item = x;
	}
}

const arg_item *arg__iter_next(struct arg__iter *it)
{
	while (it->sp) {
		const arg_item *x = it->stack[it->sp - 1];

		if (x->kind == ARG_I_END) {
			it->sp--;
			continue;
		}
		it->stack[it->sp - 1] = x + 1;
		if (x->kind != ARG_I_USE)
			return x;
		if (!x->items)
			iter_bad(it, x, "ARG_USE without a tree");
		else if (it->sp > ARG__USE_MAX)
			iter_bad(it, x, "ARG_USE nested too deeply");
		else
			it->stack[it->sp++] = x->items;
	}
	return NULL;
}

/* ------------------------------------------------------------------ */
/* Small predicates                                                   */
/* ------------------------------------------------------------------ */

static bool is_option(arg_item_kind k)
{
	return k == ARG_I_FLAG || k == ARG_I_COUNT || k == ARG_I_VALUE ||
	       k == ARG_I_DO || k == ARG_I_ON;
}

static bool has_long(const arg_item *it)
{
	return it->name && it->name[0];
}

/* Printable ASCII except '-' (SDD 7.1). */
static bool short_ok(char c)
{
	return c > ' ' && c < 0x7f && c != '-';
}

static bool long_ok(const char *s)
{
	size_t n = strlen(s);

	return n > 0 && n <= UINT16_MAX - 3 && s[0] != '-' &&
	       !strchr(s, '=');
}

static bool is_int_to(arg_to_type t)
{
	return t == ARG_TO_T_INT || t == ARG_TO_T_LONG || t == ARG_TO_T_I64 ||
	       t == ARG_TO_T_UINT || t == ARG_TO_T_ULONG ||
	       t == ARG_TO_T_U64 || t == ARG_TO_T_SIZE;
}

/* ------------------------------------------------------------------ */
/* Subcommand names                                                   */
/* ------------------------------------------------------------------ */

/* Yields every subcommand name and ARG_ALIAS of a command in order. */
struct names {
	struct arg__iter outer, inner;
	const arg_item *sub;
	bool in;
};

static void names_init(struct names *e, const arg_item *cmd)
{
	arg__iter_init(&e->outer, cmd);
	e->sub = NULL;
	e->in = false;
}

static const char *names_next(struct names *e)
{
	const arg_item *x;

	for (;;) {
		if (e->in) {
			while ((x = arg__iter_next(&e->inner))) {
				if (x->kind == ARG_I_ALIAS && x->name)
					return x->name;
			}
			e->in = false;
		}
		do {
			x = arg__iter_next(&e->outer);
			if (!x)
				return NULL;
		} while (x->kind != ARG_I_CMD);
		e->sub = x;
		arg__iter_init(&e->inner, x);
		e->in = true;
		if (x->name)
			return x->name;
	}
}

const arg_item *arg__sub_lookup(const arg_item *cmd, const char *s, size_t n)
{
	struct names e;
	const char *name;

	names_init(&e, cmd);
	while ((name = names_next(&e))) {
		if (strlen(name) == n && memcmp(name, s, n) == 0)
			return e.sub;
	}
	return NULL;
}

/* ------------------------------------------------------------------ */
/* Spec errors                                                        */
/* ------------------------------------------------------------------ */

static arg_status spec_err(arg *p, const arg_item *cmd, const arg_item *item,
			   const char *detail)
{
	arg_error *e = &arg__priv(p)->err;

	memset(e, 0, sizeof(*e));
	e->status = ARG_E_SPEC;
	e->argi = -1;
	e->cmd = cmd;
	e->item = item;
	e->detail = detail;
	return ARG_E_SPEC;
}

/* ------------------------------------------------------------------ */
/* Item checks                                                        */
/* ------------------------------------------------------------------ */

static const char *check_type(const arg_item *it)
{
	const arg_type *t = arg__value_type(it);

	if (!t)
		return NULL;
	if (!t->parse || t->size == 0)
		return "type without parse function or size";
	if (it->kind == ARG_I_FLAG && t->kind != ARG_K_BOOL)
		return "flag type must be a bool type";
	if (t->kind == ARG_K_ENUM && !it->names)
		return "enum without choices";
	return NULL;
}

static const char *check_option(const arg_item *it)
{
	if (!it->short_name && !has_long(it))
		return "option without a short or long name";
	if (it->short_name && !short_ok(it->short_name))
		return "invalid short option character";
	if (has_long(it) && !long_ok(it->name))
		return "invalid long option name";
	if (it->alias && !long_ok(it->alias))
		return "invalid option alias";
	if (it->kind == ARG_I_DO && !it->fn.on_do)
		return "ARG_DO without a function";
	if (it->kind == ARG_I_ON && !it->fn.on_each)
		return "ARG_ON without a function";
	if (it->kind == ARG_I_ON && it->many)
		return "ARG_ON cannot collect a list";
	return check_type(it);
}

static const char *check_to(const arg_item *it)
{
	arg_to_type tt = it->to.type;
	const arg_type *t = arg__value_type(it);
	bool ok;

	if (tt == ARG_TO_T_NONE)
		return NULL;
	if ((unsigned)tt > (unsigned)ARG_TO_T_RAW)
		return "invalid .to binding type";
	if (!it->to.ptr)
		return ".to binding without a variable";
	if (!t)
		return ".to binding on an item without a value";
	if (tt == ARG_TO_T_RAW)
		return NULL;
	switch (it->kind == ARG_I_COUNT ? ARG_K_INT : t->kind) {
	case ARG_K_BOOL:
		ok = tt == ARG_TO_T_BOOL;
		break;
	case ARG_K_INT:
	case ARG_K_UINT:
	case ARG_K_ENUM:
		ok = is_int_to(tt);
		break;
	case ARG_K_DOUBLE:
		ok = tt == ARG_TO_T_DOUBLE || tt == ARG_TO_T_FLOAT;
		break;
	case ARG_K_STR:
		ok = tt == ARG_TO_T_STR;
		break;
	default:
		ok = false;
		break;
	}
	return ok ? NULL : ".to binding does not fit the item type";
}

/* Parse s[0..n) with the item's type into dst; true on success. */
static bool parses(arg *p, const arg_item *it, const char *s, size_t n,
		   void *dst)
{
	const arg_type *t = arg__value_type(it);
	arg_ctx ctx = { NULL, NULL, NULL, ARG_E_BAD_VALUE };

	ctx.parser = p;
	ctx.item = it;
	return t->parse(t, s, n, dst, &ctx) == 0;
}

static bool def_parses(arg *p, const arg_item *it, void *tmp)
{
	const char *s = it->def;
	size_t i, start = 0, n = strlen(s);

	if (!it->many || !it->sep)
		return parses(p, it, s, n, tmp);
	for (i = 0; i <= n; i++) {
		if (i < n && s[i] != it->sep)
			continue;
		if (!parses(p, it, s + start, i - start, tmp))
			return false;
		start = i + 1;
	}
	return true;
}

static const char *check_bound(arg *p, const arg_item *it, const char *s,
			       void *tmp)
{
	int cmp;

	if (!parses(p, it, s, strlen(s), tmp))
		return "bound does not parse with the item's type";
	if (!arg__compare(arg__value_type(it), tmp, tmp, &cmp))
		return ".min/.max need a numeric type";
	return NULL;
}

/* .def, .min and .max are text parsed by the item's type (SDD 5.2). */
static const char *check_texts(arg *p, const arg_item *it, bool *oom)
{
	const arg_type *t = arg__value_type(it);
	const char *e = NULL;
	struct arg__arena mark;
	void *tmp;

	if (!t) {
		if (it->def || it->min || it->max)
			return ".def/.min/.max on an item without a value";
		return NULL;
	}
	if (!it->def && !it->min && !it->max)
		return NULL;
	arg__arena_mark(p, &mark);
	tmp = arg__alloc(p, t->size);
	if (!tmp) {
		*oom = true;
		return NULL;
	}
	if (it->def && !def_parses(p, it, tmp))
		e = "default does not parse with the item's type";
	if (!e && it->min)
		e = check_bound(p, it, it->min, tmp);
	if (!e && it->max)
		e = check_bound(p, it, it->max, tmp);
	arg__arena_release(p, &mark);
	return e;
}

static bool has_handler_or_sub(const arg_item *cmd)
{
	struct arg__iter it;
	const arg_item *x;

	if (cmd->fn.run)
		return true;
	arg__iter_init(&it, cmd);
	while ((x = arg__iter_next(&it))) {
		if (x->kind == ARG_I_CMD || x->kind == ARG_I_MAIN)
			return true;
	}
	return false;
}

/* ------------------------------------------------------------------ */
/* Pass 1: check every child and count                                */
/* ------------------------------------------------------------------ */

static const char *check_child(struct arg__cmd *c, const arg_item *x)
{
	switch (x->kind) {
	case ARG_I_FLAG:
	case ARG_I_COUNT:
	case ARG_I_VALUE:
	case ARG_I_DO:
	case ARG_I_ON:
		return check_option(x);
	case ARG_I_ARG:
		if (!x->name || !x->name[0])
			return "positional without a name";
		return check_type(x);
	case ARG_I_CMD:
		if (!x->name || !x->name[0])
			return "command without a name";
		if (c->level + 1 > ARG_MAX_DEPTH)
			return "commands nested deeper than ARG_MAX_DEPTH";
		if (!has_handler_or_sub(x))
			return "command without handler or subcommands";
		return NULL;
	case ARG_I_ALIAS:
		return x->name && x->name[0] ? NULL : "alias without a name";
	case ARG_I_MAIN:
		if (!x->fn.run)
			return "ARG_MAIN without a function";
		return c->main ? "more than one ARG_MAIN" : NULL;
	case ARG_I_GROUP:
	case ARG_I_RULE:
	case ARG_I_EXAMPLE:
		return NULL;
	default:
		return "unknown item kind";
	}
}

static void count_child(struct arg__cmd *c, const arg_item *x)
{
	if (is_option(x->kind)) {
		c->nopt++;
		c->nlong += (size_t)has_long(x) + (size_t)(x->alias != NULL);
		if (x->kind == ARG_I_FLAG && x->negatable && has_long(x))
			c->nlong++;
	} else if (x->kind == ARG_I_ARG) {
		c->npos++;
	} else if (x->kind == ARG_I_CMD) {
		c->nsub++;
	} else if (x->kind == ARG_I_MAIN) {
		c->main = x;
	}
}

static arg_status scan(arg *p, struct arg__cmd *c)
{
	struct arg__iter it;
	const arg_item *x;
	const char *e;
	bool oom = false;
	arg_status st;

	arg__iter_init(&it, c->cmd);
	while ((x = arg__iter_next(&it))) {
		e = check_child(c, x);
		if (!e && (is_option(x->kind) || x->kind == ARG_I_ARG))
			e = check_to(x);
		if (!e && (is_option(x->kind) || x->kind == ARG_I_ARG))
			e = check_texts(p, x, &oom);
		if (oom)
			return arg__set_error(p, ARG_E_NO_MEMORY, x, NULL);
		if (e)
			return spec_err(p, c->cmd, x, e);
		if (x->kind == ARG_I_RULE) {
			st = arg__rules_compile(c->cmd, x, &arg__priv(p)->err);
			if (st != ARG_OK)
				return st;
		}
		count_child(c, x);
	}
	if (it.error)
		return spec_err(p, c->cmd, it.error_item, it.error);
	if (c->nopt + c->npos > UINT16_MAX)
		return spec_err(p, c->cmd, NULL, "too many items in a command");
	return ARG_OK;
}

/* ------------------------------------------------------------------ */
/* Long names                                                         */
/* ------------------------------------------------------------------ */

static const char *long_name(const struct arg__cmd *c,
			     const struct arg__long *e)
{
	const arg_item *it = c->st[e->st].item;

	return e->form == ARG__LONG_ALIAS ? it->alias : it->name;
}

static unsigned char long_byte(const struct arg__cmd *c,
			       const struct arg__long *e, size_t k)
{
	const char *name = long_name(c, e);

	if (e->form != ARG__LONG_NEG)
		return (unsigned char)name[k];
	return (unsigned char)(k < 3 ? "no-"[k] : name[k - 3]);
}

/* Order of entry e relative to the name s[0..n): (length, bytes). */
static int long_cmp_str(const struct arg__cmd *c, const struct arg__long *e,
			const char *s, size_t n)
{
	size_t k;

	if (e->len != n)
		return e->len < n ? -1 : 1;
	for (k = 0; k < n; k++) {
		unsigned char a = long_byte(c, e, k), b = (unsigned char)s[k];

		if (a != b)
			return a < b ? -1 : 1;
	}
	return 0;
}

static int long_cmp(const struct arg__cmd *c, const struct arg__long *a,
		    const struct arg__long *b)
{
	size_t k;

	if (a->len != b->len)
		return a->len < b->len ? -1 : 1;
	for (k = 0; k < a->len; k++) {
		unsigned char x = long_byte(c, a, k), y = long_byte(c, b, k);

		if (x != y)
			return x < y ? -1 : 1;
	}
	return 0;
}

static void long_add(struct arg__cmd *c, size_t *n, size_t st,
		     const char *name, uint8_t form)
{
	struct arg__long *e = &c->longs[(*n)++];

	e->st = (uint16_t)st;
	e->len = (uint16_t)(strlen(name) + (form == ARG__LONG_NEG ? 3 : 0));
	e->form = form;
}

static void long_sort(struct arg__cmd *c)
{
	size_t i, j;

	for (i = 1; i < c->nlong; i++) {
		struct arg__long e = c->longs[i];

		for (j = i; j > 0 && long_cmp(c, &c->longs[j - 1], &e) > 0; j--)
			c->longs[j] = c->longs[j - 1];
		c->longs[j] = e;
	}
}

/*
 * Drop generated "no-" names shadowed by a real name (SDD 7.2 rule 9);
 * two real names that are equal are a spec error.
 */
static arg_status long_dedup(arg *p, struct arg__cmd *c)
{
	size_t i, w = 0;

	for (i = 0; i < c->nlong; i++) {
		struct arg__long *prev = w ? &c->longs[w - 1] : NULL;
		struct arg__long *e = &c->longs[i];

		if (!prev || long_cmp(c, prev, e) != 0) {
			c->longs[w++] = *e;
		} else if (prev->form == ARG__LONG_NEG) {
			*prev = *e;
		} else if (e->form != ARG__LONG_NEG) {
			return spec_err(p, c->cmd, c->st[e->st].item,
					"duplicate long option name");
		}
	}
	c->nlong = w;
	return ARG_OK;
}

struct arg__state *arg__long_lookup(struct arg__cmd *c, const char *s,
				    size_t n, bool *neg)
{
	size_t lo = 0, hi = c->nlong;

	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		const struct arg__long *e = &c->longs[mid];
		int r = long_cmp_str(c, e, s, n);

		if (r == 0) {
			if (neg)
				*neg = e->form == ARG__LONG_NEG;
			return &c->st[e->st];
		}
		if (r < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return NULL;
}

/* ------------------------------------------------------------------ */
/* Short names                                                        */
/* ------------------------------------------------------------------ */

struct arg__state *arg__short_lookup(struct arg__cmd *c, char ch)
{
	unsigned char u = (unsigned char)ch;
	size_t i;

	if (!c->shorts || u >= 128 || c->shorts[u] == 0)
		return NULL;
	if (c->shorts[u] != ARG__SHORT_FAR)
		return &c->st[c->shorts[u] - 1];
	/* only an option at index >= 254 sets the marker */
	for (i = ARG__SHORT_FAR - 1; c->st[i].item->short_name != ch; i++)
		;
	return &c->st[i];
}

static arg_status short_add(arg *p, struct arg__cmd *c, size_t i)
{
	const arg_item *it = c->st[i].item;
	unsigned char u = (unsigned char)it->short_name;

	if (arg__short_lookup(c, it->short_name))
		return spec_err(p, c->cmd, it, "duplicate short option");
	c->shorts[u] = (uint8_t)(i + 1 < ARG__SHORT_FAR ? i + 1
							: ARG__SHORT_FAR);
	if (u >= '0' && u <= '9') {
		c->digit_short = true;
		c->digit_global = c->digit_global || it->global;
	}
	return ARG_OK;
}

/* ------------------------------------------------------------------ */
/* Pass 2: fill the index                                             */
/* ------------------------------------------------------------------ */

static bool any_short(const arg_item *cmd)
{
	struct arg__iter it;
	const arg_item *x;

	arg__iter_init(&it, cmd);
	while ((x = arg__iter_next(&it))) {
		if (is_option(x->kind) && x->short_name)
			return true;
	}
	return false;
}

/* The short table costs 128 bytes, so only commands with shorts get one. */
static arg_status alloc_tables(arg *p, struct arg__cmd *c)
{
	size_t n = c->nopt + c->npos;

	c->st = arg__alloc(p, n * sizeof(*c->st));
	c->longs = arg__alloc(p, c->nlong * sizeof(*c->longs));
	if (any_short(c->cmd))
		c->shorts = arg__alloc(p, 128);
	if (!c->st || !c->longs || (!c->shorts && any_short(c->cmd)))
		return arg__set_error(p, ARG_E_NO_MEMORY, NULL, NULL);
	memset(c->st, 0, n * sizeof(*c->st));
	if (c->shorts)
		memset(c->shorts, 0, 128);
	return ARG_OK;
}

static arg_status add_option(arg *p, struct arg__cmd *c, size_t i,
			     size_t *nl)
{
	const arg_item *x = c->st[i].item;

	if (has_long(x))
		long_add(c, nl, i, x->name, ARG__LONG_NAME);
	if (x->alias)
		long_add(c, nl, i, x->alias, ARG__LONG_ALIAS);
	if (x->kind == ARG_I_FLAG && x->negatable && has_long(x))
		long_add(c, nl, i, x->name, ARG__LONG_NEG);
	return x->short_name ? short_add(p, c, i) : ARG_OK;
}

static arg_status fill(arg *p, struct arg__cmd *c)
{
	size_t io = 0, ip = c->nopt, nl = 0;
	struct arg__iter it;
	const arg_item *x;
	arg_status st;

	st = alloc_tables(p, c);
	if (st != ARG_OK)
		return st;
	arg__iter_init(&it, c->cmd);
	while ((x = arg__iter_next(&it))) {
		if (x->kind == ARG_I_ARG) {
			c->st[ip++].item = x;
		} else if (is_option(x->kind)) {
			c->st[io].item = x;
			st = add_option(p, c, io++, &nl);
			if (st != ARG_OK)
				return st;
		}
	}
	long_sort(c);
	return long_dedup(p, c);
}

/* ------------------------------------------------------------------ */
/* Whole-command checks                                               */
/* ------------------------------------------------------------------ */

static bool keyed(const arg_item *x)
{
	return is_option(x->kind) || x->kind == ARG_I_ARG ||
	       x->kind == ARG_I_CMD;
}

static arg_status check_keys(arg *p, struct arg__cmd *c)
{
	struct arg__iter a, b;
	const arg_item *x, *y;
	char kx[3], ky[3];

	arg__iter_init(&a, c->cmd);
	while ((x = arg__iter_next(&a))) {
		if (!keyed(x))
			continue;
		b = a;
		while ((y = arg__iter_next(&b))) {
			if (keyed(y) && strcmp(arg__item_key(x, kx),
					       arg__item_key(y, ky)) == 0)
				return spec_err(p, c->cmd, y,
						"duplicate lookup key");
		}
	}
	return ARG_OK;
}

static arg_status check_subs(arg *p, struct arg__cmd *c)
{
	struct names a, b;
	const char *x, *y;

	names_init(&a, c->cmd);
	while ((x = names_next(&a))) {
		b = a;
		while ((y = names_next(&b))) {
			if (strcmp(x, y) == 0)
				return spec_err(p, c->cmd, b.sub,
						"duplicate command name");
		}
	}
	return ARG_OK;
}

static arg_status check_positionals(arg *p, struct arg__cmd *c)
{
	bool variadic = false, optional = false;
	size_t i;

	for (i = c->nopt; i < c->nopt + c->npos; i++) {
		const arg_item *x = c->st[i].item;

		if (variadic)
			return spec_err(p, c->cmd, x,
					"positional after a variadic one");
		if (x->min_count && optional)
			return spec_err(p, c->cmd, x,
					"required positional after an "
					"optional one");
		if (x->many && x->max_count && x->max_count < x->min_count)
			return spec_err(p, c->cmd, x,
					".max_count below the minimum");
		variadic = x->many;
		optional = optional || !x->min_count;
	}
	return ARG_OK;
}

arg_status arg__index(arg *p, const arg_item *cmd, struct arg__cmd *parent,
		      struct arg__cmd **out)
{
	struct arg__cmd *c = arg__alloc(p, sizeof(*c));
	arg_status st;

	if (!c)
		return arg__set_error(p, ARG_E_NO_MEMORY, NULL, NULL);
	memset(c, 0, sizeof(*c));
	c->cmd = cmd;
	c->parent = parent;
	c->level = parent ? parent->level + 1 : 0;
	st = scan(p, c);
	if (st == ARG_OK)
		st = fill(p, c);
	if (st == ARG_OK)
		st = check_subs(p, c);
	if (st == ARG_OK)
		st = check_keys(p, c);
	if (st == ARG_OK)
		st = check_positionals(p, c);
	if (st == ARG_OK)
		*out = c;
	return st;
}

/* ------------------------------------------------------------------ */
/* Lookups for other modules                                          */
/* ------------------------------------------------------------------ */

struct arg__state *arg__find_option(struct arg__cmd *c, const char *name)
{
	struct arg__state *s;
	bool neg = false;

	if (!name || !name[0])
		return NULL;
	if (name[0] == '-' && name[1] && !name[2])
		return arg__short_lookup(c, name[1]);
	s = arg__long_lookup(c, name, strlen(name), &neg);
	return neg ? NULL : s;
}

struct arg__state *arg__state_of(arg *p, const arg_item *item)
{
	struct arg__priv *v = arg__priv(p);
	size_t d, i;

	for (d = 0; d < v->depth; d++) {
		struct arg__cmd *c = v->idx[d];

		for (i = 0; c && i < c->nopt + c->npos; i++) {
			if (c->st[i].item == item)
				return &c->st[i];
		}
	}
	return NULL;
}

static void selected_into(const struct arg__priv *v, struct arg__pathbuf *out)
{
	size_t i;

	out->path.depth = v->depth;
	for (i = 0; i < v->depth; i++)
		out->cmds[i] = v->cmds[i];
	if (!v->depth && v->root) {
		out->cmds[0] = v->root;
		out->path.depth = 1;
	}
}

const arg_item *arg__find_cmd(arg *p, const char *path,
			      struct arg__pathbuf *out)
{
	const struct arg__priv *v = arg__priv(p);
	const arg_item *cmd = v->root;
	size_t n;

	out->path.prog = v->prog;
	out->path.cmds = out->cmds;
	out->path.depth = 0;
	if (!cmd)
		return NULL;
	if (!path) {
		selected_into(v, out);
		return out->cmds[out->path.depth - 1];
	}
	out->cmds[0] = cmd;
	out->path.depth = 1;
	for (;;) {
		while (*path == ' ')
			path++;
		if (!*path)
			return cmd;
		for (n = 0; path[n] && path[n] != ' '; n++)
			;
		if (out->path.depth > ARG_MAX_DEPTH)
			return NULL;
		cmd = arg__sub_lookup(cmd, path, n);
		if (!cmd)
			return NULL;
		out->cmds[out->path.depth++] = cmd;
		path += n;
	}
}

static bool dfs(const arg_item *at, const arg_item *want,
		struct arg__pathbuf *out, size_t depth)
{
	struct arg__iter it;
	const arg_item *x;

	out->cmds[depth] = at;
	if (at == want) {
		out->path.depth = depth + 1;
		return true;
	}
	if (depth >= ARG_MAX_DEPTH)
		return false;
	arg__iter_init(&it, at);
	while ((x = arg__iter_next(&it))) {
		if (x->kind == ARG_I_CMD && dfs(x, want, out, depth + 1))
			return true;
	}
	return false;
}

bool arg__cmd_path(arg *p, const arg_item *cmd, struct arg__pathbuf *out)
{
	const struct arg__priv *v = arg__priv(p);
	size_t i;

	out->path.prog = v->prog;
	out->path.cmds = out->cmds;
	out->path.depth = 0;
	for (i = 0; i < v->depth; i++) {
		out->cmds[i] = v->cmds[i];
		if (v->cmds[i] == cmd) {
			out->path.depth = i + 1;
			return true;
		}
	}
	return v->root && dfs(v->root, cmd, out, 0);
}

/* ------------------------------------------------------------------ */
/* Whole-tree operations                                              */
/* ------------------------------------------------------------------ */

static arg_status compile_cmd(arg *p, const arg_item *cmd,
			      struct arg__cmd *parent)
{
	struct arg__arena mark;
	struct arg__iter it;
	struct arg__cmd *c;
	const arg_item *x;
	arg_status st;

	arg__arena_mark(p, &mark);
	st = arg__index(p, cmd, parent, &c);
	arg__iter_init(&it, cmd);
	while (st == ARG_OK && (x = arg__iter_next(&it))) {
		if (x->kind == ARG_I_CMD)
			st = compile_cmd(p, x, c);
	}
	arg__arena_release(p, &mark);
	return st;
}

arg_status arg_compile(const arg_item *items, const arg_config *cfg,
		       arg_error *err)
{
	arg_parser tmp;
	arg_config c;
	arg_status st;

	memset(&c, 0, sizeof(c));
	if (cfg)
		c = *cfg;
	else
		c.version = ARG_CONFIG_VERSION;
#ifndef ARG_FREESTANDING
	/* a whole tree may not fit the arena; this is a test-time tool */
	if (!c.alloc)
		c.alloc = arg_alloc_stdlib;
#endif
	arg__init(&tmp, items, &c, 0);
	if (!items || items[0].kind != ARG_I_CMD)
		st = spec_err(&tmp, NULL, items,
			      "root must be an ARG_I_CMD item");
	else
		st = compile_cmd(&tmp, items, NULL);
	if (err) {
		*err = arg__priv(&tmp)->err;
		err->status = st;
	}
	arg_free(&tmp);
	return st;
}

static int visit(const arg_item *x, const arg_item *parent, unsigned depth,
		 arg_visit_fn fn, void *ud)
{
	struct arg__iter it;
	const arg_item *y;
	int r = fn(ud, x, parent, depth);

	if (r || x->kind != ARG_I_CMD || depth > ARG_MAX_DEPTH)
		return r;
	arg__iter_init(&it, x);
	while ((y = arg__iter_next(&it))) {
		r = visit(y, x, depth + 1, fn, ud);
		if (r)
			return r;
	}
	return 0;
}

/* root is items[0] as for arg_init(); Level 1 arrays have no ARG_I_END. */
int arg_foreach_item(const arg_item *root, arg_visit_fn fn, void *ud)
{
	if (!root || !fn)
		return 0;
	return visit(root, NULL, 0, fn, ud);
}
