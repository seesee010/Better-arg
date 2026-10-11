// SPDX-License-Identifier: GPL-2.0-only
/*
 * store.c - arena and value storage (SDD 13)
 *
 * All working memory of a parse comes from one bump allocator: first
 * the inline arena of arg_parser (or cfg.arena when the caller gives
 * one), then blocks from cfg.alloc, chained so that arg_free() can
 * return them. Nothing is ever freed individually.
 *
 * Values are stored in the layout of their type: a single value in a
 * block of type->size bytes, a list (.many, ARG_ARGS) in chunks that
 * double in capacity. Strings point into argv; only the pieces of a
 * split value (.sep) are copied, NUL-terminated, into the arena.
 */
#include "arg_internal.h"

/* ------------------------------------------------------------------ */
/* Arena                                                              */
/* ------------------------------------------------------------------ */

union arg__maxalign {
	void *p;
	uint64_t u;
	double d;
};

struct arg__align_probe {
	char c;
	union arg__maxalign m;
};

#define ARENA_ALIGN offsetof(struct arg__align_probe, m)
#define BLOCK_MIN 1024u

static size_t round_up(size_t n)
{
	return (n + ARENA_ALIGN - 1) / ARENA_ALIGN * ARENA_ALIGN;
}

/* Bytes needed to align p. */
static size_t pad_for(const unsigned char *p)
{
	size_t mis = (size_t)((uintptr_t)p % ARENA_ALIGN);

	return mis ? ARENA_ALIGN - mis : 0;
}

static void *bump(struct arg__arena *a, size_t n)
{
	size_t pad, left;
	void *r;

	pad = pad_for(a->buf + a->used);
	left = a->cap - a->used;
	if (pad > left || n > left - pad)
		return NULL;
	r = a->buf + a->used + pad;
	a->used += pad + n;
	return r;
}

/* Get a new block from cfg.alloc that can hold n bytes. */
static bool grow(struct arg__priv *v, size_t n)
{
	struct arg__arena *a = &v->arena;
	size_t hdr = round_up(sizeof(struct arg__block)), size;
	struct arg__block *b;

	if (!v->cfg.alloc || n > SIZE_MAX / 4)
		return false;
	size = BLOCK_MIN;
	if (a->blocks && a->blocks->size < SIZE_MAX / 4)
		size = a->blocks->size * 2; /* fewer blocks for big parses */
	while (size < hdr + n + ARENA_ALIGN)
		size *= 2;
	b = v->cfg.alloc(v->cfg.alloc_ud, NULL, 0, size);
	if (!b)
		return false;
	b->next = a->blocks;
	b->size = size;
	a->blocks = b;
	a->buf = (unsigned char *)b + hdr;
	a->cap = size - hdr;
	a->used = 0;
	return true;
}

void *arg__alloc(arg *p, size_t n)
{
	struct arg__priv *v = arg__priv(p);
	void *r;

	if (n == 0)
		n = 1;
	r = bump(&v->arena, n);
	if (r || !grow(v, n))
		return r;
	return bump(&v->arena, n);
}

void arg__arena_reset(arg *p)
{
	struct arg__priv *v = arg__priv(p);
	struct arg__arena *a = &v->arena;

	while (a->blocks) {
		struct arg__block *b = a->blocks;

		a->blocks = b->next;
		if (v->cfg.alloc)
			v->cfg.alloc(v->cfg.alloc_ud, b, b->size, 0);
	}
	if (v->cfg.arena && v->cfg.arena_cap) {
		a->buf = v->cfg.arena;
		a->cap = v->cfg.arena_cap;
	} else {
		a->buf = p->arena_inline.bytes;
		a->cap = sizeof(p->arena_inline.bytes);
	}
	a->used = 0;
}

void arg__arena_mark(arg *p, struct arg__arena *m)
{
	*m = arg__priv(p)->arena;
}

void arg__arena_release(arg *p, const struct arg__arena *m)
{
	struct arg__priv *v = arg__priv(p);
	struct arg__arena *a = &v->arena;

	while (a->blocks && a->blocks != m->blocks) {
		struct arg__block *b = a->blocks;

		a->blocks = b->next;
		v->cfg.alloc(v->cfg.alloc_ud, b, b->size, 0);
	}
	*a = *m;
}

/* ------------------------------------------------------------------ */
/* Lists                                                              */
/* ------------------------------------------------------------------ */

static struct arg__chunk *chunk_new(arg *p, size_t size, size_t cap)
{
	struct arg__chunk *c;

	if (size && cap > SIZE_MAX / size)
		return NULL;
	c = arg__alloc(p, sizeof(*c));
	if (!c)
		return NULL;
	c->data = arg__alloc(p, cap * size);
	if (!c->data)
		return NULL;
	c->next = NULL;
	c->n = 0;
	c->cap = cap;
	return c;
}

/*
 * Room for one more element of size bytes; the element counts only
 * after list_commit(), so a failed conversion leaves no trace.
 */
static void *list_reserve(arg *p, struct arg__chunk **head, size_t size,
			  struct arg__chunk **chunk)
{
	struct arg__chunk *c = *head;

	if (!c) {
		c = chunk_new(p, size, 4);
		if (!c)
			return NULL;
		*head = c;
	}
	while (c->next)
		c = c->next;
	if (c->n == c->cap) {
		struct arg__chunk *nc = chunk_new(p, size, c->cap * 2);

		if (!nc)
			return NULL;
		c->next = nc;
		c = nc;
	}
	*chunk = c;
	return c->data + c->n * size;
}

static size_t list_len(const struct arg__chunk *c)
{
	size_t n = 0;

	for (; c; c = c->next)
		n += c->n;
	return n;
}

static const void *list_at(const struct arg__chunk *c, size_t size, size_t i)
{
	for (; c; c = c->next) {
		if (i < c->n)
			return c->data + i * size;
		i -= c->n;
	}
	return NULL;
}

arg_status arg__rest_push(arg *p, const char *s)
{
	struct arg__priv *v = arg__priv(p);
	struct arg__chunk *c;
	void *slot = list_reserve(p, &v->rest, sizeof(s), &c);

	if (!slot)
		return arg__set_error(p, ARG_E_NO_MEMORY, NULL, NULL);
	memcpy(slot, &s, sizeof(s));
	c->n++;
	return ARG_OK;
}

size_t arg_rest_count(const arg *a)
{
	return list_len(arg__cpriv(a)->rest);
}

const char *arg_rest_at(const arg *a, size_t i)
{
	const void *slot = list_at(arg__cpriv(a)->rest, sizeof(char *), i);
	const char *s = NULL;

	if (slot)
		memcpy(&s, slot, sizeof(s));
	return s;
}

/* ------------------------------------------------------------------ */
/* Items                                                              */
/* ------------------------------------------------------------------ */

const arg_type *arg__value_type(const arg_item *it)
{
	switch (it->kind) {
	case ARG_I_FLAG:
		return it->type ? it->type : &arg_t_bool;
	case ARG_I_COUNT:
		return &arg_t_u32;
	case ARG_I_VALUE:
	case ARG_I_ARG:
		return it->type ? it->type : &arg_t_string;
	default:
		return NULL;
	}
}

const char *arg__item_key(const arg_item *it, char buf[3])
{
	if (it->id)
		return it->id;
	if (it->name && it->name[0])
		return it->name;
	buf[0] = '-';
	buf[1] = it->short_name;
	buf[2] = '\0';
	return buf;
}

size_t arg__state_len(const struct arg__state *s)
{
	if (s->item->kind == ARG_I_COUNT)
		return s->source != ARG_SRC_NONE;
	if (s->item->many)
		return list_len(s->v.list);
	return s->v.one != NULL;
}

const void *arg__state_value(const struct arg__state *s, size_t i)
{
	const arg_type *t = arg__value_type(s->item);

	if (!t || s->item->kind == ARG_I_COUNT)
		return i == 0 && s->source != ARG_SRC_NONE ? &s->count : NULL;
	if (s->item->many)
		return list_at(s->v.list, t->size, i);
	return i == 0 ? s->v.one : NULL;
}

bool arg__state_is_set(const arg *p, const struct arg__state *s)
{
	return (arg__cpriv(p)->cfg.rule_sources & ARG_SRC_BIT(s->source)) &&
	       s->source != ARG_SRC_NONE;
}

/* ------------------------------------------------------------------ */
/* Conversion                                                         */
/* ------------------------------------------------------------------ */

static arg_status convert(arg *p, const arg_item *it, const char *s,
			  size_t n, void *dst, int argi)
{
	const arg_type *t = arg__value_type(it);
	arg_ctx ctx = { NULL, NULL, NULL, ARG_E_BAD_VALUE };
	struct arg__priv *v;
	arg_status st;

	ctx.parser = p;
	ctx.item = it;
	if (t->parse(t, s, n, dst, &ctx) == 0)
		return ARG_OK;
	st = ctx.status == ARG_OK ? ARG_E_BAD_VALUE : ctx.status;
	arg__set_error(p, st, it, ctx.detail);
	v = arg__priv(p);
	v->err.text = s;
	v->err.text_len = n;
	v->err.argi = argi;
	return st;
}

static arg_status store_one(arg *p, struct arg__state *s, const char *text,
			    size_t n, int argi)
{
	const arg_type *t = arg__value_type(s->item);
	struct arg__chunk *c;
	void *dst;
	arg_status st;

	if (!s->item->many) {
		if (!s->v.one)
			s->v.one = arg__alloc(p, t->size);
		if (!s->v.one)
			return arg__set_error(p, ARG_E_NO_MEMORY, s->item,
					      NULL);
		return convert(p, s->item, text, n, s->v.one, argi);
	}
	dst = list_reserve(p, &s->v.list, t->size, &c);
	if (!dst)
		return arg__set_error(p, ARG_E_NO_MEMORY, s->item, NULL);
	st = convert(p, s->item, text, n, dst, argi);
	if (st == ARG_OK)
		c->n++;
	return st;
}

/* NUL-terminated arena copy of s[0..n). */
static char *dup_piece(arg *p, const char *s, size_t n)
{
	char *d = arg__alloc(p, n + 1);

	if (d) {
		memcpy(d, s, n);
		d[n] = '\0';
	}
	return d;
}

arg_status arg__store_text(arg *p, struct arg__state *s, const char *text,
			   size_t n, int argi)
{
	const arg_item *it = s->item;
	size_t i, start = 0;
	arg_status st;

	if (it->kind == ARG_I_COUNT) {
		uint32_t level;

		st = convert(p, it, text, n, &level, argi);
		if (st == ARG_OK)
			s->count = level;
		return st;
	}
	if (!it->many || !it->sep)
		return store_one(p, s, text, n, argi);
	for (i = 0; i <= n; i++) {
		char *piece;

		if (i < n && text[i] != it->sep)
			continue;
		piece = dup_piece(p, text + start, i - start);
		if (!piece)
			return arg__set_error(p, ARG_E_NO_MEMORY, it, NULL);
		st = store_one(p, s, piece, i - start, argi);
		if (st != ARG_OK)
			return st;
		start = i + 1;
	}
	return ARG_OK;
}
