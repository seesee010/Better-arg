// SPDX-License-Identifier: GPL-2.0-only
/*
 * rules.c - command rules (SDD 10)
 *
 * Rule items (ARG_MUTEX, ARG_ONE_OF, ARG_ANY_OF, ARG_REQUIRES,
 * ARG_CONFLICTS, ARG_RULE_FN) are checked twice:
 *
 *   - arg__rules_compile() runs while the spec compiler builds the
 *     index of the command that holds the rule. Every member name must
 *     name an option of that same command: its long name, its .alias
 *     or "-x" for its short name, the lookup arg__find_option() uses.
 *     Options spliced in with ARG_USE count. Options of ancestors
 *     (even .global ones) do not; put the rule into the command that
 *     declares the options.
 *
 *   - arg__rules_check() runs after the values are final. It walks
 *     the selected path from the root to the deepest command and
 *     evaluates the rules of each command in declaration order. An
 *     option counts as set when arg__state_is_set() says so, which
 *     honours cfg.rule_sources (SDD 10.2). The first violation stops.
 *
 * Messages name the options the way error output does ("--json", or
 * "-x" for short-only options). They are built in the parser arena;
 * when that is exhausted a static generic text is used instead, so a
 * rule violation never turns into ARG_E_NO_MEMORY.
 */
#include "arg_internal.h"

/* ------------------------------------------------------------------ */
/* Spec time                                                          */
/* ------------------------------------------------------------------ */

static bool is_option(arg_item_kind k)
{
	return k == ARG_I_FLAG || k == ARG_I_COUNT || k == ARG_I_VALUE ||
	       k == ARG_I_DO || k == ARG_I_ON;
}

static bool str_eq(const char *a, const char *b)
{
	return a && b && strcmp(a, b) == 0;
}

/* Same matching as arg__find_option(), but on the raw spec. */
static bool names_option(const arg_item *x, const char *name)
{
	if (!is_option(x->kind) || !name[0])
		return false;
	if (name[0] == '-' && name[1] && !name[2])
		return x->short_name == name[1];
	return (x->name && x->name[0] && str_eq(x->name, name)) ||
	       str_eq(x->alias, name);
}

static const arg_item *spec_option(const arg_item *cmd, const char *name)
{
	struct arg__iter it;
	const arg_item *x;

	arg__iter_init(&it, cmd);
	while ((x = arg__iter_next(&it))) {
		if (names_option(x, name))
			return x;
	}
	return NULL;
}

static size_t count_names(const char *const *names)
{
	size_t n = 0;

	while (names && names[n])
		n++;
	return n;
}

static const char *check_members(const arg_item *cmd, const arg_item *rule,
				 const char **bad)
{
	const char *const *names = rule->names;
	size_t i, j, n = count_names(names);

	for (i = 0; i < n; i++) {
		const arg_item *x = spec_option(cmd, names[i]);

		*bad = names[i];
		if (!x)
			return "rule names an unknown option";
		for (j = 0; j < i; j++) {
			if (spec_option(cmd, names[j]) == x)
				return "rule names an option twice";
		}
	}
	*bad = NULL;
	return NULL;
}

static const char *check_rule(const arg_item *cmd, const arg_item *rule,
			      const char **bad)
{
	*bad = NULL;
	switch (rule->rule_kind) {
	case ARG_R_MUTEX:
	case ARG_R_ONE_OF:
	case ARG_R_ANY_OF:
	case ARG_R_REQUIRES:
	case ARG_R_CONFLICTS:
		if (count_names(rule->names) < 2)
			return "rule needs at least two options";
		break;
	case ARG_R_CUSTOM:
		if (!rule->fn.rule)
			return "custom rule without a function";
		break;
	case ARG_R_NONE:
	default:
		return "unknown rule kind";
	}
	return check_members(cmd, rule, bad);
}

arg_status arg__rules_compile(const arg_item *cmd, const arg_item *rule,
			      arg_error *err)
{
	const char *bad;
	const char *e = check_rule(cmd, rule, &bad);

	if (!e)
		return ARG_OK;
	if (err) {
		memset(err, 0, sizeof(*err));
		err->status = ARG_E_SPEC;
		err->argi = -1;
		err->cmd = cmd;
		err->item = rule;
		err->detail = e;
		if (bad) {
			err->text = bad;
			err->text_len = strlen(bad);
		}
	}
	return ARG_E_SPEC;
}

/* ------------------------------------------------------------------ */
/* Messages                                                           */
/* ------------------------------------------------------------------ */

/* Counts on the first pass (buf NULL), fills on the second. */
struct mbuf {
	char *buf;
	size_t len;
};

static int mbuf_write(void *ud, const char *s, size_t n)
{
	struct mbuf *b = ud;

	if (b->buf)
		memcpy(b->buf + b->len, s, n);
	b->len += n;
	return 0;
}

static void put_str(struct mbuf *b, const char *s)
{
	mbuf_write(b, s, strlen(s));
}

static void put_label(struct mbuf *b, const arg_item *it)
{
	put_str(b, "'");
	arg__write_item_label(mbuf_write, b, it, false);
	put_str(b, "'");
}

enum msg_kind {
	MSG_TOGETHER, /* options 'x' and 'y' cannot be used together */
	MSG_ONE_OF,   /* exactly one of 'a', 'b' is required         */
	MSG_ANY_OF,   /* at least one of 'a', 'b' is required        */
	MSG_REQUIRES, /* option 'x' requires 'y'                     */
	MSG_CONFLICTS /* option 'x' conflicts with 'y'               */
};

static const char *const msg_fallback[] = {
	[MSG_TOGETHER] = "options cannot be used together",
	[MSG_ONE_OF] = "exactly one of the options is required",
	[MSG_ANY_OF] = "at least one of the options is required",
	[MSG_REQUIRES] = "option requires another option",
	[MSG_CONFLICTS] = "option conflicts with another option",
};

struct msg {
	enum msg_kind kind;
	struct arg__cmd *c;
	const arg_item *rule;
	const arg_item *x, *y;
};

static void put_list(struct mbuf *b, const struct msg *m)
{
	const char *const *n;

	for (n = m->rule->names; *n; n++) {
		struct arg__state *s = arg__find_option(m->c, *n);

		if (n != m->rule->names)
			put_str(b, ", ");
		put_label(b, s ? s->item : NULL);
	}
}

static void compose(struct mbuf *b, const struct msg *m)
{
	switch (m->kind) {
	case MSG_TOGETHER:
		put_str(b, "options ");
		put_label(b, m->x);
		put_str(b, " and ");
		put_label(b, m->y);
		put_str(b, " cannot be used together");
		break;
	case MSG_ONE_OF:
		put_str(b, "exactly one of ");
		put_list(b, m);
		put_str(b, " is required");
		break;
	case MSG_ANY_OF:
		put_str(b, "at least one of ");
		put_list(b, m);
		put_str(b, " is required");
		break;
	case MSG_REQUIRES:
		put_str(b, "option ");
		put_label(b, m->x);
		put_str(b, " requires ");
		put_label(b, m->y);
		break;
	case MSG_CONFLICTS:
		put_str(b, "option ");
		put_label(b, m->x);
		put_str(b, " conflicts with ");
		put_label(b, m->y);
		break;
	}
}

static const char *message(arg *p, const struct msg *m)
{
	struct mbuf b = { NULL, 0 };

	compose(&b, m);
	b.buf = arg__alloc(p, b.len + 1);
	if (!b.buf)
		return msg_fallback[m->kind];
	b.len = 0;
	compose(&b, m);
	b.buf[b.len] = '\0';
	return b.buf;
}

static arg_status violate(arg *p, struct msg *m, const arg_item *item)
{
	return arg__set_error(p, ARG_E_RULE, item, message(p, m));
}

/* ------------------------------------------------------------------ */
/* Run time                                                           */
/* ------------------------------------------------------------------ */

/* Member i of the rule; NULL only for a rule the compiler never saw. */
static const arg_item *member(struct arg__cmd *c, const arg_item *rule,
			      size_t i, bool *set, arg *p)
{
	struct arg__state *s = arg__find_option(c, rule->names[i]);

	*set = s && arg__state_is_set(p, s);
	return s ? s->item : NULL;
}

/* MUTEX, ONE_OF, ANY_OF: count the members that are set. */
static arg_status check_count(arg *p, struct arg__cmd *c, const arg_item *rule)
{
	struct msg m = { MSG_TOGETHER, NULL, NULL, NULL, NULL };
	size_t i, n = 0;
	bool set;

	m.c = c;
	m.rule = rule;
	for (i = 0; rule->names[i]; i++) {
		const arg_item *it = member(c, rule, i, &set, p);

		if (!set)
			continue;
		if (n++ == 0) {
			m.x = it;
		} else if (rule->rule_kind != ARG_R_ANY_OF) {
			m.y = it;
			return violate(p, &m, it);
		}
	}
	if (n > 0 || rule->rule_kind == ARG_R_MUTEX)
		return ARG_OK;
	m.kind = rule->rule_kind == ARG_R_ONE_OF ? MSG_ONE_OF : MSG_ANY_OF;
	return violate(p, &m, member(c, rule, 0, &set, p));
}

/* REQUIRES, CONFLICTS: names[0] set => all / none of the others. */
static arg_status check_implies(arg *p, struct arg__cmd *c,
				const arg_item *rule)
{
	struct msg m = { MSG_REQUIRES, NULL, NULL, NULL, NULL };
	bool want = rule->rule_kind == ARG_R_REQUIRES;
	size_t i;
	bool set;

	m.c = c;
	m.rule = rule;
	m.x = member(c, rule, 0, &set, p);
	if (!set)
		return ARG_OK;
	if (!want)
		m.kind = MSG_CONFLICTS;
	for (i = 1; rule->names[i]; i++) {
		m.y = member(c, rule, i, &set, p);
		if (set != want)
			return violate(p, &m, m.y);
	}
	return ARG_OK;
}

static arg_status check_custom(arg *p, const arg_item *rule)
{
	(void)arg__take_fail(p);
	if (rule->fn.rule(p, rule) == 0)
		return ARG_OK;
	return arg__set_error(p, ARG_E_RULE, NULL, arg__take_fail(p));
}

static arg_status check_one(arg *p, struct arg__cmd *c, const arg_item *rule)
{
	switch (rule->rule_kind) {
	case ARG_R_MUTEX:
	case ARG_R_ONE_OF:
	case ARG_R_ANY_OF:
		return check_count(p, c, rule);
	case ARG_R_REQUIRES:
	case ARG_R_CONFLICTS:
		return check_implies(p, c, rule);
	case ARG_R_CUSTOM:
		return check_custom(p, rule);
	case ARG_R_NONE:
	default:
		/* rejected by arg__rules_compile() */
		return ARG_OK;
	}
}

arg_status arg__rules_check(arg *p)
{
	struct arg__priv *v = arg__priv(p);
	struct arg__iter it;
	const arg_item *x;
	arg_status st;
	size_t d;

	for (d = 0; d < v->depth; d++) {
		struct arg__cmd *c = v->idx[d];

		arg__iter_init(&it, c->cmd);
		while ((x = arg__iter_next(&it))) {
			if (x->kind != ARG_I_RULE)
				continue;
			st = check_one(p, c, x);
			if (st != ARG_OK)
				return st;
		}
	}
	return ARG_OK;
}
