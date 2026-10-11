// SPDX-License-Identifier: GPL-2.0-only
/*
 * getters.c - reading values by key (SDD 5.3, 6.4)
 *
 * A key is the item's .id, else its long name, else "-x"; positionals
 * and commands go by name. Lookup starts at the deepest selected
 * command and walks up to the root, which is how handlers reach
 * .global options (and every other option of their ancestors).
 *
 * Misuse (unknown key, a getter that does not fit the item) returns
 * the zero value. Debug builds also print one diagnostic per parser
 * to write_err, naming the key and the command path.
 */
#include "arg_internal.h"

struct found {
	struct arg__state *s; /* option or positional         */
	const arg_item *cmd;  /* or a command on the path     */
};

static bool key_is(const arg_item *it, const char *key)
{
	char kb[3];

	return strcmp(arg__item_key(it, kb), key) == 0;
}

static void diag(arg *a, const char *what, const char *key)
{
#ifndef NDEBUG
	struct arg__priv *v = arg__priv(a);
	arg_write_fn w = v->cfg.write_err;
	void *ud = v->cfg.io_ud;
	struct arg__path path;

	if (v->diag_done)
		return;
	v->diag_done = true;
	arg__selected_path(a, &path);
	if (w(ud, "arg: ", 5) || w(ud, what, strlen(what)) ||
	    w(ud, " '", 2) || arg__write_escaped(w, ud, key, strlen(key)) ||
	    w(ud, "' in '", 6) || arg__write_path(w, ud, &path))
		return;
	w(ud, "'\n", 2);
#else
	(void)a;
	(void)what;
	(void)key;
#endif
}

static bool lookup(arg *a, const char *key, struct found *f)
{
	struct arg__priv *v = arg__priv(a);
	size_t d, i;

	f->s = NULL;
	f->cmd = NULL;
	if (!key)
		key = "";
	for (d = v->depth; d-- > 0;) {
		struct arg__cmd *c = v->idx[d];

		for (i = 0; c && i < c->nopt + c->npos; i++) {
			if (key_is(c->st[i].item, key)) {
				f->s = &c->st[i];
				return true;
			}
		}
		if (d > 0 && key_is(v->cmds[d], key)) {
			f->cmd = v->cmds[d];
			return true;
		}
	}
	diag(a, "unknown key", key);
	return false;
}

/* The item's value type, if its kind is one of the wanted ones. */
static const arg_type *typed(arg *a, const char *key, const struct found *f,
			     unsigned kinds)
{
	const arg_type *t;

	if (f->s && (t = arg__value_type(f->s->item)) &&
	    (kinds & (1u << t->kind)))
		return t;
	diag(a, "wrong getter for key", key);
	return NULL;
}

#define K(k) (1u << (k))

static bool is_counter(const struct found *f)
{
	return f->s && f->s->item->kind == ARG_I_COUNT;
}

bool arg_flag(arg *a, const char *key)
{
	struct found f;
	const void *src;
	bool b = false;

	if (!lookup(a, key, &f))
		return false;
	if (f.cmd)
		return true;
	switch (f.s->item->kind) {
	case ARG_I_COUNT:
	case ARG_I_DO:
	case ARG_I_ON:
		return f.s->count > 0;
	default:
		break;
	}
	if (!typed(a, key, &f, K(ARG_K_BOOL)))
		return false;
	src = arg__state_value(f.s, 0);
	if (src)
		memcpy(&b, src, sizeof(b));
	return b;
}

int64_t arg_int_at(arg *a, const char *key, size_t i)
{
	struct found f;
	const arg_type *t;
	int64_t r = 0;

	if (!lookup(a, key, &f))
		return 0;
	if (is_counter(&f))
		return i == 0 ? (int64_t)f.s->count : 0;
	t = typed(a, key, &f, K(ARG_K_INT) | K(ARG_K_ENUM));
	if (t && !arg__load_int(t, arg__state_value(f.s, i), &r))
		r = 0;
	return r;
}

int64_t arg_int(arg *a, const char *key)
{
	return arg_int_at(a, key, 0);
}

uint64_t arg_uint_at(arg *a, const char *key, size_t i)
{
	struct found f;
	const arg_type *t;
	uint64_t r = 0;

	if (!lookup(a, key, &f))
		return 0;
	if (is_counter(&f))
		return i == 0 ? f.s->count : 0;
	t = typed(a, key, &f, K(ARG_K_UINT));
	if (t && !arg__load_uint(t, arg__state_value(f.s, i), &r))
		r = 0;
	return r;
}

uint64_t arg_uint(arg *a, const char *key)
{
	return arg_uint_at(a, key, 0);
}

double arg_double_at(arg *a, const char *key, size_t i)
{
	struct found f;
	const arg_type *t;
	double r = 0;

	if (!lookup(a, key, &f))
		return 0;
	t = typed(a, key, &f, K(ARG_K_DOUBLE));
	if (t && !arg__load_double(t, arg__state_value(f.s, i), &r))
		r = 0;
	return r;
}

double arg_double(arg *a, const char *key)
{
	return arg_double_at(a, key, 0);
}

/* The word an ARG_ENUM value stands for, or NULL. */
static const char *enum_word(const struct found *f, const void *src)
{
	const char *const *names = f->s->item->names;
	int idx = -1, n;

	if (!src || !names)
		return NULL;
	memcpy(&idx, src, sizeof(idx));
	for (n = 0; names[n]; n++)
		if (n == idx)
			return names[n];
	return NULL;
}

const char *arg_str_at(arg *a, const char *key, size_t i)
{
	struct found f;
	const arg_type *t;
	const void *src;
	const char *s = NULL;

	if (!lookup(a, key, &f))
		return NULL;
	t = typed(a, key, &f, K(ARG_K_STR) | K(ARG_K_ENUM));
	if (!t)
		return NULL;
	src = arg__state_value(f.s, i);
	if (t->kind == ARG_K_ENUM)
		return enum_word(&f, src);
	if (src)
		memcpy(&s, src, sizeof(s));
	return s;
}

const char *arg_str(arg *a, const char *key)
{
	return arg_str_at(a, key, 0);
}

int arg_enum(arg *a, const char *key)
{
	struct found f;
	const void *src;
	int r = -1;

	if (!lookup(a, key, &f) || !typed(a, key, &f, K(ARG_K_ENUM)))
		return -1;
	src = arg__state_value(f.s, 0);
	if (src)
		memcpy(&r, src, sizeof(r));
	return r;
}

size_t arg_count(arg *a, const char *key)
{
	struct found f;

	if (!lookup(a, key, &f))
		return 0;
	if (f.cmd)
		return 1;
	switch (f.s->item->kind) {
	case ARG_I_COUNT:
	case ARG_I_DO:
	case ARG_I_ON:
		return f.s->count;
	default:
		return arg__state_len(f.s);
	}
}

bool arg_get_at(arg *a, const char *key, size_t i, void *out)
{
	struct found f;
	const arg_type *t;
	const void *src;

	if (!lookup(a, key, &f))
		return false;
	t = typed(a, key, &f, ~0u);
	src = t ? arg__state_value(f.s, i) : NULL;
	if (!src)
		return false;
	memcpy(out, src, t->size);
	return true;
}

bool arg_get(arg *a, const char *key, void *out)
{
	return arg_get_at(a, key, 0, out);
}

arg_source_kind arg_source(arg *a, const char *key)
{
	struct found f;

	if (!lookup(a, key, &f))
		return ARG_SRC_NONE;
	if (f.cmd)
		return ARG_SRC_CLI;
	return (arg_source_kind)f.s->source;
}

bool arg_is_set(arg *a, const char *key)
{
	arg_source_kind s = arg_source(a, key);

	return s == ARG_SRC_CLI || s == ARG_SRC_SOURCE || s == ARG_SRC_ENV;
}

const char *arg_path(arg *a)
{
	struct arg__priv *v = arg__priv(a);
	struct arg__path path;
	size_t i, n = 0, k;
	const char *root;
	char *s;

	if (v->path_str)
		return v->path_str;
	arg__selected_path(a, &path);
	root = path.prog ? path.prog : path.cmds[0]->name;
	if (!root)
		root = "";
	n = strlen(root);
	for (i = 1; i < path.depth; i++)
		n += 1 + strlen(path.cmds[i]->name);
	s = arg__alloc(a, n + 1);
	if (!s)
		return NULL;
	k = strlen(root);
	memcpy(s, root, k);
	for (i = 1; i < path.depth; i++) {
		size_t m = strlen(path.cmds[i]->name);

		s[k++] = ' ';
		memcpy(s + k, path.cmds[i]->name, m);
		k += m;
	}
	s[k] = '\0';
	v->path_str = s;
	return s;
}

bool arg_path_is(arg *a, const char *path)
{
	const struct arg__priv *v = arg__priv(a);
	size_t i = 1, n;

	if (!path)
		return false;
	for (;;) {
		while (*path == ' ')
			path++;
		if (!*path)
			return i == (v->depth ? v->depth : 1);
		for (n = 0; path[n] && path[n] != ' '; n++)
			;
		if (i >= v->depth || strlen(v->cmds[i]->name) != n ||
		    memcmp(v->cmds[i]->name, path, n) != 0)
			return false;
		path += n;
		i++;
	}
}

void *arg_udata(arg *a)
{
	return arg__priv(a)->cfg.udata;
}
