// SPDX-License-Identifier: GPL-2.0-only
/*
 * complete.c - shell completion scripts (SDD 11.3)
 *
 * arg_completion() writes a static completion script for bash, zsh or
 * fish. It only needs arg_init(): the whole spec tree is walked from
 * the root (ARG_USE spliced in by arg__iter), and the program name is
 * the root command's name, not argv[0].
 *
 * Every command reachable through visible subcommands becomes a
 * numbered node (pre-order, root = 0). Each script carries a small
 * state machine that replays the words before the cursor: options
 * that take a value skip the next word, "--" ends option parsing, and
 * a word that names a subcommand of the current node (while no
 * positional was given to it, as in parse.c) moves to that node. The
 * node and the number of positionals then select the candidates:
 * options (own ones, .global ones of ancestors, automatic -h/--help
 * and -V/--version), subcommands and aliases, and value candidates
 * (enum choices, file / directory / host / user hints).
 *
 * Quoting: every string taken from the spec is written inside single
 * quotes, with ' escaped for the shell ('\'' for bash and zsh, \' and
 * \\ for fish). Help texts are cut at the first newline and control
 * characters become spaces. Names (programs, options, commands,
 * aliases, choices) are also used unquoted by the shells' completion
 * machinery (compgen -W, fish -a, zsh specs), so a name containing a
 * character outside [A-Za-z0-9._+:-] is skipped; a root command with
 * such a name makes arg_completion() fail.
 *
 * No heap: output goes through a small buffer on the stack.
 */
#include "arg_internal.h"

/* What to complete for a value. */
enum act_kind { A_NONE, A_FILE, A_DIR, A_HOST, A_USER, A_WORDS };

struct act {
	enum act_kind kind;
	const char *const *words; /* A_WORDS, NULL-terminated */
};

/* One spelling of an option. */
struct form {
	char c;           /* short option, 0 for a long form */
	bool neg;         /* "--no-" + name                  */
	const char *name; /* long name or alias              */
};

struct gen {
	arg_write_fn w;
	void *ud;
	int err;  /* first write error */
	bool fish;
	const struct arg__priv *v;
	const char *prog;
	size_t d;    /* depth of the current node */
	unsigned id; /* its number                */
	bool more;   /* list separator state      */
	const arg_item *path[ARG_MAX_DEPTH + 1];
	size_t n;
	char buf[256];
};

typedef void (*node_fn)(struct gen *g);
typedef void (*opt_fn)(struct gen *g, const arg_item *it);

/* ------------------------------------------------------------------ */
/* Output                                                             */
/* ------------------------------------------------------------------ */

static void flush(struct gen *g)
{
	if (g->n && !g->err)
		g->err = g->w(g->ud, g->buf, g->n);
	g->n = 0;
}

static void put_c(struct gen *g, char c)
{
	if (g->n == sizeof(g->buf))
		flush(g);
	g->buf[g->n++] = c;
}

static void put_s(struct gen *g, const char *s)
{
	while (*s)
		put_c(g, *s++);
}

/* Template text: '~' stands for the program name as an identifier. */
static void put_t(struct gen *g, const char *s)
{
	const char *p;

	for (; *s; s++) {
		if (*s != '~') {
			put_c(g, *s);
			continue;
		}
		for (p = g->prog; *p; p++) {
			bool alnum = (*p >= 'a' && *p <= 'z') ||
				     (*p >= 'A' && *p <= 'Z') ||
				     (*p >= '0' && *p <= '9');

			put_c(g, alnum ? *p : '_');
		}
	}
}

static void put_u(struct gen *g, unsigned u)
{
	char b[12];
	size_t i = sizeof(b);

	b[--i] = '\0';
	do {
		b[--i] = (char)('0' + u % 10);
		u /= 10;
	} while (u);
	put_s(g, b + i);
}

#define Q_LINE 0x1u  /* help text: first line only               */
#define Q_ZSPEC 0x2u /* zsh _arguments field: escape \ [ ] :     */
#define Q_ZNAME 0x4u /* zsh option name: also + = and a final -  */

/* Text inside single quotes, escaped for the shell. */
static void put_q(struct gen *g, const char *s, unsigned fl)
{
	for (; s && *s; s++) {
		char c = *s;

		if (c == '\n' && (fl & Q_LINE))
			break;
		if ((unsigned char)c < 0x20 || c == 0x7f)
			c = ' ';
		if ((fl & Q_ZSPEC) &&
		    (c == '\\' || c == '[' || c == ']' || c == ':'))
			put_c(g, '\\');
		if ((fl & Q_ZNAME) &&
		    (c == '+' || c == '=' || (c == '-' && !s[1])))
			put_c(g, '\\');
		if (c == '\'') {
			put_s(g, g->fish ? "\\'" : "'\\''");
			continue;
		}
		if (c == '\\' && g->fish)
			put_c(g, '\\');
		put_c(g, c);
	}
}

/* A whole single-quoted word. */
static void put_qw(struct gen *g, const char *s, unsigned fl)
{
	put_c(g, '\'');
	put_q(g, s, fl);
	put_c(g, '\'');
}

/* Separator for space separated lists. */
static void put_sep(struct gen *g)
{
	if (g->more)
		put_c(g, ' ');
	g->more = true;
}

/* ------------------------------------------------------------------ */
/* Spec model                                                         */
/* ------------------------------------------------------------------ */

static bool safe_ch(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	       (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '+' ||
	       c == ':' || c == '-';
}

static bool safe(const char *s)
{
	if (!s || !*s)
		return false;
	for (; *s; s++) {
		if (!safe_ch(*s))
			return false;
	}
	return true;
}

static bool safe_short(char c)
{
	return c != '-' && safe_ch(c);
}

static bool streq(const char *a, const char *b)
{
	return a && b && strcmp(a, b) == 0;
}

static bool is_opt(const arg_item *x)
{
	return x->kind == ARG_I_FLAG || x->kind == ARG_I_COUNT ||
	       x->kind == ARG_I_VALUE || x->kind == ARG_I_DO ||
	       x->kind == ARG_I_ON;
}

/* 0: no value, 1: required value, 2: optional (attached) value. */
static int val_mode(const arg_item *x)
{
	if (x->kind != ARG_I_VALUE &&
	    !(x->kind == ARG_I_ON && (x->value || x->optional_value)))
		return 0;
	return x->optional_value ? 2 : 1;
}

static bool repeatable(const arg_item *x)
{
	return x->kind == ARG_I_COUNT || x->kind == ARG_I_ON || x->many;
}

static bool is_sub(const arg_item *x)
{
	return x->kind == ARG_I_CMD && !x->hidden && safe(x->name);
}

static struct act act_of(enum act_kind k, const char *const *words)
{
	struct act a;

	a.kind = k;
	a.words = words;
	return a;
}

/* Candidates for a value of option or positional it. */
static struct act value_act(const arg_item *it)
{
	const arg_type *t = arg__value_type(it);
	unsigned i;

	if (it->hidden)
		return act_of(A_NONE, NULL);
	for (i = 0; t && i < 8; i++) {
		if (t->kind == ARG_K_ENUM && it->names)
			return act_of(A_WORDS, it->names);
		if (t->parse == arg_one_of_parse && t->ud)
			return act_of(A_WORDS, (const char *const *)t->ud);
		if (t->hints & ARG_HINT_FILE)
			return act_of(A_FILE, NULL);
		if (t->hints & ARG_HINT_DIR)
			return act_of(A_DIR, NULL);
		if (t->hints & ARG_HINT_HOST)
			return act_of(A_HOST, NULL);
		if (t->hints & ARG_HINT_USER)
			return act_of(A_USER, NULL);
		if (t->ud && (t->parse == arg_nonempty_parse ||
			      t->parse == arg_maxlen_parse)) {
			t = ((const arg_wrap *)t->ud)->inner;
			continue;
		}
		return act_of(t->kind == ARG_K_STR || t->kind == ARG_K_CUSTOM ?
			      A_FILE : A_NONE, NULL);
	}
	/* no type (ARG_ON values) or a broken wrapper: plain text */
	return act_of(A_FILE, NULL);
}

/* The safe words of a, separated by spaces, each quoted if wanted. */
static void put_words(struct gen *g, const struct act *a, unsigned fl,
		      bool quote)
{
	const char *const *w;

	g->more = false;
	for (w = a->words; *w; w++) {
		if (!safe(*w))
			continue;
		put_sep(g);
		if (quote)
			put_qw(g, *w, fl);
		else
			put_q(g, *w, fl);
	}
}

static size_t get_forms(const arg_item *it, struct form f[4])
{
	size_t n = 0;

	if (safe_short(it->short_name)) {
		f[n].c = it->short_name;
		f[n].neg = false;
		f[n++].name = NULL;
	}
	if (safe(it->name)) {
		f[n].c = 0;
		f[n].neg = false;
		f[n++].name = it->name;
	}
	if (safe(it->alias)) {
		f[n].c = 0;
		f[n].neg = false;
		f[n++].name = it->alias;
	}
	if (safe(it->name) && it->kind == ARG_I_FLAG && it->negatable) {
		f[n].c = 0;
		f[n].neg = true;
		f[n++].name = it->name;
	}
	return n;
}

static void put_form(struct gen *g, const struct form *f, unsigned fl)
{
	char s[2];

	if (f->c) {
		s[0] = f->c;
		s[1] = '\0';
		put_c(g, '-');
		put_q(g, s, fl);
		return;
	}
	put_s(g, f->neg ? "--no-" : "--");
	put_q(g, f->name, fl);
}

/* Options reachable in the current node, hidden ones included. */
struct opt_it {
	const struct gen *g;
	size_t lvl;
	struct arg__iter it;
};

static void opt_init(struct opt_it *o, const struct gen *g)
{
	o->g = g;
	o->lvl = g->d;
	arg__iter_init(&o->it, g->path[g->d]);
}

static const arg_item *opt_next(struct opt_it *o)
{
	const arg_item *x;

	for (;;) {
		while ((x = arg__iter_next(&o->it))) {
			if (is_opt(x) && (o->lvl == o->g->d || x->global))
				return x;
		}
		if (o->lvl == 0)
			return NULL;
		o->lvl--;
		arg__iter_init(&o->it, o->g->path[o->lvl]);
	}
}

static bool taken_short(const struct gen *g, char c)
{
	struct opt_it o;
	const arg_item *x;

	opt_init(&o, g);
	while ((x = opt_next(&o))) {
		if (x->short_name == c)
			return true;
	}
	return false;
}

static bool taken_long(const struct gen *g, const char *name)
{
	struct opt_it o;
	const arg_item *x;

	opt_init(&o, g);
	while ((x = opt_next(&o))) {
		if (streq(x->name, name) || streq(x->alias, name))
			return true;
	}
	return false;
}

/*
 * Automatic -h/--help or -V/--version as a pseudo item, unless user
 * options of the node already own both spellings (they win in parse.c).
 */
static bool auto_item(const struct gen *g, arg_item *x, char c,
		      const char *name, arg_msg_id msg)
{
	memset(x, 0, sizeof(*x));
	x->kind = ARG_I_DO;
	x->short_name = taken_short(g, c) ? 0 : c;
	x->name = taken_long(g, name) ? NULL : name;
	x->help = arg__msg(g->v->cfg.messages, msg);
	return x->short_name || x->name;
}

/* Call fn for every visible option of the current node. */
static void each_opt(struct gen *g, opt_fn fn)
{
	struct opt_it o;
	const arg_item *x;
	arg_item a;

	opt_init(&o, g);
	while ((x = opt_next(&o))) {
		if (!x->hidden)
			fn(g, x);
	}
	if (!(g->v->cfg.style & ARG_NO_AUTO_HELP) &&
	    auto_item(g, &a, 'h', "help", ARG_MSG_HELP_FLAG))
		fn(g, &a);
	if (g->v->cfg.version_text &&
	    auto_item(g, &a, 'V', "version", ARG_MSG_VERSION_FLAG))
		fn(g, &a);
}

/* Number of nodes in the subtree of cmd at depth d. */
static unsigned count_nodes(const arg_item *cmd, size_t d)
{
	struct arg__iter it;
	const arg_item *x;
	unsigned n = 1;

	if (d >= ARG_MAX_DEPTH)
		return n;
	arg__iter_init(&it, cmd);
	while ((x = arg__iter_next(&it))) {
		if (is_sub(x))
			n += count_nodes(x, d + 1);
	}
	return n;
}

/* Visible subcommands of the current node with their node numbers. */
struct kid_it {
	struct arg__iter it;
	size_t d;
	unsigned id, next;
	bool leaf;
};

static void kid_init(struct kid_it *k, const struct gen *g)
{
	arg__iter_init(&k->it, g->path[g->d]);
	k->d = g->d;
	k->next = g->id + 1;
	k->id = 0;
	k->leaf = g->d >= ARG_MAX_DEPTH;
}

static const arg_item *kid_next(struct kid_it *k)
{
	const arg_item *x;

	if (k->leaf)
		return NULL;
	while ((x = arg__iter_next(&k->it))) {
		if (!is_sub(x))
			continue;
		k->id = k->next;
		k->next += count_nodes(x, k->d + 1);
		return x;
	}
	return NULL;
}

/* Next visible ARG_ALIAS name of command cmd. */
static const char *alias_next(struct arg__iter *it)
{
	const arg_item *x;

	while ((x = arg__iter_next(it))) {
		if (x->kind == ARG_I_ALIAS && !x->hidden && safe(x->name))
			return x->name;
	}
	return NULL;
}

static bool has_subs(const struct gen *g)
{
	struct kid_it k;

	kid_init(&k, g);
	return kid_next(&k) != NULL;
}

/* i-th positional of the current node, NULL past the last. */
static const arg_item *positional(const struct gen *g, size_t i)
{
	struct arg__iter it;
	const arg_item *x;

	arg__iter_init(&it, g->path[g->d]);
	while ((x = arg__iter_next(&it))) {
		if (x->kind == ARG_I_ARG && i-- == 0)
			return x;
	}
	return NULL;
}

/* Pre-order walk over all nodes; g->path, g->d and g->id are set. */
static void walk(struct gen *g, size_t d, unsigned id, node_fn fn)
{
	struct kid_it k;
	const arg_item *x;

	g->d = d;
	g->id = id;
	fn(g);
	kid_init(&k, g);
	while ((x = kid_next(&k))) {
		g->path[d + 1] = x;
		walk(g, d + 1, k.id, fn);
		g->d = d;
		g->id = id;
	}
}

/* ------------------------------------------------------------------ */
/* bash                                                               */
/* ------------------------------------------------------------------ */

static const char bash_head[] =
	"# _~_act ACTION WORD: add the candidates of ACTION that match WORD\n"
	"_~_act()\n"
	"{\n"
	"\tlocal l a\n"
	"\tcase \"$1\" in\n"
	"\tf) a=(-f) ;;\n"
	"\td) a=(-d) ;;\n"
	"\th) a=(-A hostname) ;;\n"
	"\tu) a=(-u) ;;\n"
	"\tw:*) a=(-W \"${1#w:}\") ;;\n"
	"\t*) return 0 ;;\n"
	"\tesac\n"
	"\tcase \"$1\" in\n"
	"\tf | d) compopt -o filenames 2>/dev/null ;;\n"
	"\tesac\n"
	"\twhile IFS= read -r l; do\n"
	"\t\tCOMPREPLY+=(\"$l\")\n"
	"\tdone < <(compgen \"${a[@]}\" -- \"$2\")\n"
	"}\n"
	"\n"
	"# _~_vact NODE OPTION: set va to the value action of OPTION\n"
	"# (empty: no value, leading '=': optional attached value)\n"
	"_~_vact()\n"
	"{\n"
	"\tcase \"$1:$2\" in\n";

static const char bash_pact_head[] =
	"\t*) va= ;;\n"
	"\tesac\n"
	"}\n"
	"\n"
	"# _~_pact NODE N: set va to the action of the next positional\n"
	"_~_pact()\n"
	"{\n"
	"\tcase \"$1:$2\" in\n";

static const char bash_node_head[] =
	"\t*) va= ;;\n"
	"\tesac\n"
	"}\n"
	"\n"
	"# _~_node NODE: set opts and subs\n"
	"_~_node()\n"
	"{\n"
	"\tcase \"$1\" in\n";

static const char bash_main[] =
	"\tesac\n"
	"}\n"
	"\n"
	"_~()\n"
	"{\n"
	"\tlocal cur=\"${COMP_WORDS[COMP_CWORD]}\" s=0 np=0 dd=0 skip=0\n"
	"\tlocal i j w vo va opts subs\n"
	"\tCOMPREPLY=()\n"
	"\tfor ((i = 1; i < COMP_CWORD; i++)); do\n"
	"\t\tw=\"${COMP_WORDS[i]}\"\n"
	"\t\tif ((skip)); then\n"
	"\t\t\t# \"--opt=value\" split at '=' by COMP_WORDBREAKS\n"
	"\t\t\t[[ $w == = && $vo == --* ]] || skip=0\n"
	"\t\t\tcontinue\n"
	"\t\tfi\n"
	"\t\tif ((!dd)); then\n"
	"\t\t\tcase \"$w\" in\n"
	"\t\t\t--)\n"
	"\t\t\t\tdd=1\n"
	"\t\t\t\tcontinue\n"
	"\t\t\t\t;;\n"
	"\t\t\t--*=*)\n"
	"\t\t\t\tcontinue\n"
	"\t\t\t\t;;\n"
	"\t\t\t--*)\n"
	"\t\t\t\t_~_vact \"$s\" \"$w\"\n"
	"\t\t\t\t[[ -n $va && $va != =* ]] && skip=1 vo=\"$w\"\n"
	"\t\t\t\tcontinue\n"
	"\t\t\t\t;;\n"
	"\t\t\t-?*)\n"
	"\t\t\t\tfor ((j = 1; j < ${#w}; j++)); do\n"
	"\t\t\t\t\t_~_vact \"$s\" \"-${w:j:1}\"\n"
	"\t\t\t\t\t[[ -n $va ]] || continue\n"
	"\t\t\t\t\tif [[ $va != =* ]] && ((j == ${#w} - 1)); then\n"
	"\t\t\t\t\t\tskip=1 vo=\"-${w:j:1}\"\n"
	"\t\t\t\t\tfi\n"
	"\t\t\t\t\tbreak\n"
	"\t\t\t\tdone\n"
	"\t\t\t\tcontinue\n"
	"\t\t\t\t;;\n"
	"\t\t\tesac\n"
	"\t\tfi\n"
	"\t\tif ((np == 0)); then\n"
	"\t\t\tcase \"$s:$w\" in\n";

static const char bash_tail[] =
	"\t\t\tesac\n"
	"\t\tfi\n"
	"\t\tnp=$((np + 1))\n"
	"\tdone\n"
	"\tif ((skip)); then\n"
	"\t\t[[ $cur == = ]] && cur=\n"
	"\t\t_~_vact \"$s\" \"$vo\"\n"
	"\t\t_~_act \"$va\" \"$cur\"\n"
	"\t\treturn 0\n"
	"\tfi\n"
	"\tif ((!dd)); then\n"
	"\t\tcase \"$cur\" in\n"
	"\t\t--*=*)\n"
	"\t\t\t_~_vact \"$s\" \"${cur%%=*}\"\n"
	"\t\t\t_~_act \"${va#=}\" \"${cur#*=}\"\n"
	"\t\t\tCOMPREPLY=(\"${COMPREPLY[@]/#/${cur%%=*}=}\")\n"
	"\t\t\treturn 0\n"
	"\t\t\t;;\n"
	"\t\t-*)\n"
	"\t\t\t_~_node \"$s\"\n"
	"\t\t\t_~_act \"w:$opts\" \"$cur\"\n"
	"\t\t\treturn 0\n"
	"\t\t\t;;\n"
	"\t\tesac\n"
	"\tfi\n"
	"\t_~_node \"$s\"\n"
	"\tif ((np == 0)); then\n"
	"\t\t_~_act \"w:$subs\" \"$cur\"\n"
	"\tfi\n"
	"\t_~_pact \"$s\" \"$np\"\n"
	"\t_~_act \"$va\" \"$cur\"\n"
	"\treturn 0\n"
	"}\n"
	"\n"
	"complete -F _~ ";

/* bash action word, inside quotes: n f d h u w:WORDS */
static void bash_act(struct gen *g, const struct act *a)
{
	static const char letter[] = "nfdhuw";

	put_c(g, letter[a->kind]);
	if (a->kind == A_WORDS) {
		put_c(g, ':');
		put_words(g, a, 0, false);
	}
}

/* 'NODE:' + text, quoted */
static void bash_key(struct gen *g, const struct form *f, const char *word)
{
	put_c(g, '\'');
	put_u(g, g->id);
	put_c(g, ':');
	if (f)
		put_form(g, f, 0);
	else
		put_q(g, word, 0);
	put_c(g, '\'');
}

static void bash_vact_opt(struct gen *g, const arg_item *it)
{
	struct form f[4];
	size_t i, n = get_forms(it, f);
	int m = val_mode(it);
	struct act a;
	bool first = true;

	/* value options are never negatable: no "--no-" forms here */
	if (!m)
		return;
	for (i = 0; i < n; i++) {
		put_s(g, first ? "\t" : " | ");
		first = false;
		bash_key(g, &f[i], NULL);
	}
	if (first)
		return;
	a = value_act(it);
	put_s(g, ") va='");
	if (m == 2)
		put_c(g, '=');
	bash_act(g, &a);
	put_s(g, "' ;;\n");
}

static void bash_vact(struct gen *g)
{
	each_opt(g, bash_vact_opt);
}

static void bash_pact(struct gen *g)
{
	const arg_item *x;
	struct act a;
	size_t i;

	for (i = 0; (x = positional(g, i)); i++) {
		a = value_act(x);
		if (a.kind != A_NONE) {
			put_s(g, "\t'");
			put_u(g, g->id);
			put_c(g, ':');
			if (x->many) {
				put_s(g, "'*");
			} else {
				put_u(g, (unsigned)i);
				put_c(g, '\'');
			}
			put_s(g, ") va='");
			bash_act(g, &a);
			put_s(g, "' ;;\n");
		}
		if (x->many)
			break;
	}
}

static void bash_opt_words(struct gen *g, const arg_item *it)
{
	struct form f[4];
	size_t i, n = get_forms(it, f);

	for (i = 0; i < n; i++) {
		put_sep(g);
		put_form(g, &f[i], 0);
	}
}

static void bash_node_case(struct gen *g)
{
	struct kid_it k;
	struct arg__iter it;
	const arg_item *x;
	const char *alias;

	put_c(g, '\t');
	put_u(g, g->id);
	put_s(g, ")\n\t\topts='");
	g->more = false;
	each_opt(g, bash_opt_words);
	put_s(g, "'\n\t\tsubs='");
	g->more = false;
	kid_init(&k, g);
	while ((x = kid_next(&k))) {
		put_sep(g);
		put_q(g, x->name, 0);
		arg__iter_init(&it, x);
		while ((alias = alias_next(&it))) {
			put_sep(g);
			put_q(g, alias, 0);
		}
	}
	put_s(g, "'\n\t\t;;\n");
}

static void bash_trans(struct gen *g)
{
	struct kid_it k;
	struct arg__iter it;
	const arg_item *x;
	const char *alias;

	kid_init(&k, g);
	while ((x = kid_next(&k))) {
		put_s(g, "\t\t\t");
		bash_key(g, NULL, x->name);
		arg__iter_init(&it, x);
		while ((alias = alias_next(&it))) {
			put_s(g, " | ");
			bash_key(g, NULL, alias);
		}
		put_s(g, ")\n\t\t\t\ts=");
		put_u(g, k.id);
		put_s(g, "\n\t\t\t\tcontinue\n\t\t\t\t;;\n");
	}
}

static void gen_bash(struct gen *g)
{
	put_s(g, "# bash completion for ");
	put_s(g, g->prog);
	put_s(g, "\n# Generated by Better-arg; do not edit.\n\n");
	put_t(g, bash_head);
	walk(g, 0, 0, bash_vact);
	put_t(g, bash_pact_head);
	walk(g, 0, 0, bash_pact);
	put_t(g, bash_node_head);
	walk(g, 0, 0, bash_node_case);
	put_t(g, bash_main);
	walk(g, 0, 0, bash_trans);
	put_t(g, bash_tail);
	put_qw(g, g->prog, 0);
	put_c(g, '\n');
}

/* ------------------------------------------------------------------ */
/* zsh                                                                */
/* ------------------------------------------------------------------ */

/* Action inside an _arguments spec. */
static void zsh_spec_act(struct gen *g, const struct act *a)
{
	static const char *const name[] = { " ", "_files", "_files -/",
					    "_hosts", "_users" };

	if (a->kind != A_WORDS) {
		put_s(g, name[a->kind]);
		return;
	}
	put_c(g, '(');
	put_words(g, a, Q_ZSPEC, false);
	put_c(g, ')');
}

/* Action as a command line; false for A_NONE. */
static bool zsh_cmd_act(struct gen *g, const struct act *a)
{
	static const char *const name[] = { "", "_files", "_files -/",
					    "_hosts", "_users" };

	if (a->kind == A_NONE)
		return false;
	if (a->kind != A_WORDS) {
		put_s(g, name[a->kind]);
		return true;
	}
	put_s(g, "compadd -- ");
	put_words(g, a, 0, true);
	return true;
}

static void zsh_meta(struct gen *g, const char *s)
{
	put_q(g, s && *s ? s : "VALUE", Q_ZSPEC | Q_LINE);
}

static void zsh_opt(struct gen *g, const arg_item *it)
{
	struct form f[4];
	size_t i, j, n = get_forms(it, f);
	int m = val_mode(it);
	const arg_type *t = arg__value_type(it);
	struct act a = value_act(it);

	for (i = 0; i < n; i++) {
		put_s(g, " \\\n\t\t'");
		if (repeatable(it)) {
			put_c(g, '*');
		} else {
			put_c(g, '(');
			for (j = 0; j < n; j++) {
				if (j)
					put_c(g, ' ');
				put_form(g, &f[j], Q_ZSPEC | Q_ZNAME);
			}
			put_c(g, ')');
		}
		put_form(g, &f[i], Q_ZSPEC | Q_ZNAME);
		if (m && !f[i].neg) {
			if (f[i].c)
				put_s(g, m == 1 ? "+" : "-");
			else
				put_s(g, m == 1 ? "=" : "=-");
		}
		put_c(g, '[');
		put_q(g, it->help, Q_ZSPEC | Q_LINE);
		put_c(g, ']');
		if (m && !f[i].neg) {
			put_s(g, m == 1 ? ":" : "::");
			zsh_meta(g, it->value ? it->value : t ? t->name : NULL);
			put_c(g, ':');
			zsh_spec_act(g, &a);
		}
		put_c(g, '\'');
	}
}

/* Positional specs of a node without subcommands. */
static void zsh_positionals(struct gen *g)
{
	const arg_item *x;
	struct act a;
	bool opt = false;
	size_t i;

	for (i = 0; (x = positional(g, i)); i++) {
		a = value_act(x);
		opt = opt || x->min_count == 0;
		put_s(g, " \\\n\t\t'");
		put_s(g, x->many ? "*:" : opt ? "::" : ":");
		zsh_meta(g, x->value ? x->value : x->name);
		put_c(g, ':');
		zsh_spec_act(g, &a);
		put_c(g, '\'');
		if (x->many)
			break;
	}
}

/* The action of positional i as a command, "&& ret=0" appended. */
static void zsh_pos_cmd(struct gen *g, const char *indent, size_t i)
{
	const arg_item *x = positional(g, i);
	struct act a;

	if (!x)
		return;
	a = value_act(x);
	put_s(g, indent);
	if (zsh_cmd_act(g, &a))
		put_s(g, " && ret=0\n");
	else
		put_s(g, ":\n");
}

static void zsh_cmds(struct gen *g)
{
	struct kid_it k;
	struct arg__iter it;
	const arg_item *x;
	const char *alias;
	size_t npos = 0;

	while (positional(g, npos))
		npos++;
	put_s(g, " \\\n\t\t': :->cmd' \\\n\t\t'*:: :->arg' && ret=0\n"
		 "\tcase $state in\n\tcmd)\n\t\tlocal -a cmds\n\t\tcmds=(\n");
	kid_init(&k, g);
	while ((x = kid_next(&k))) {
		arg__iter_init(&it, x);
		alias = x->name;
		do {
			put_s(g, "\t\t\t'");
			put_q(g, alias, Q_ZSPEC);
			put_c(g, ':');
			put_q(g, x->help, Q_LINE);
			put_s(g, "'\n");
		} while ((alias = alias_next(&it)));
	}
	put_s(g, "\t\t)\n\t\t_describe -t commands command cmds && ret=0\n");
	zsh_pos_cmd(g, "\t\t", 0);
	put_s(g, "\t\t;;\n\targ)\n\t\tcase $line[1] in\n");
	kid_init(&k, g);
	while ((x = kid_next(&k))) {
		put_s(g, "\t\t");
		put_qw(g, x->name, 0);
		arg__iter_init(&it, x);
		while ((alias = alias_next(&it))) {
			put_s(g, " | ");
			put_qw(g, alias, 0);
		}
		put_t(g, ")\n\t\t\t_~_");
		put_u(g, k.id);
		put_s(g, " && ret=0\n\t\t\t;;\n");
	}
	if (npos) {
		put_s(g, "\t\t*)\n");
		zsh_pos_cmd(g, "\t\t\t", npos - 1);
		put_s(g, "\t\t\t;;\n");
	}
	put_s(g, "\t\tesac\n\t\t;;\n\tesac\n\treturn ret\n");
}

static void zsh_node(struct gen *g)
{
	bool subs = has_subs(g);

	put_t(g, "_~_");
	put_u(g, g->id);
	put_s(g, "()\n{\n");
	if (subs)
		put_s(g, "\tlocal curcontext=\"$curcontext\" state line "
			 "ret=1\n\ttypeset -A opt_args\n"
			 "\t_arguments -C -s -S");
	else
		put_s(g, "\t_arguments -s -S");
	each_opt(g, zsh_opt);
	if (subs) {
		zsh_cmds(g);
	} else {
		zsh_positionals(g);
		put_c(g, '\n');
	}
	put_s(g, "}\n\n");
}

static void gen_zsh(struct gen *g)
{
	put_s(g, "#compdef ");
	put_s(g, g->prog);
	put_s(g, "\n# zsh completion for ");
	put_s(g, g->prog);
	put_s(g, "\n# Generated by Better-arg; do not edit.\n\n");
	walk(g, 0, 0, zsh_node);
	put_t(g, "_~()\n{\n\t_~_0 \"$@\"\n}\n\n"
		 "if [ \"$funcstack[1]\" = \"_~\" ]; then\n"
		 "\t_~ \"$@\"\n"
		 "else\n"
		 "\tcompdef _~ ");
	put_qw(g, g->prog, 0);
	put_s(g, "\nfi\n");
}

/* ------------------------------------------------------------------ */
/* fish                                                               */
/* ------------------------------------------------------------------ */

static const char fish_head[] =
	"# __~_vopt NODE OPTION: does OPTION take the next word as value?\n"
	"function __~_vopt\n"
	"\tswitch \"$argv[1]:$argv[2]\"\n";

static const char fish_state[] =
	"\tend\n"
	"\treturn 1\n"
	"end\n"
	"\n"
	"# __~_state: print the current node and the number of positional\n"
	"# words it has received\n"
	"function __~_state\n"
	"\tset -l w (commandline -opc)\n"
	"\tset -q w[1]; and set -e w[1]\n"
	"\tset -l s 0\n"
	"\tset -l np 0\n"
	"\tset -l skip 0\n"
	"\tset -l dd 0\n"
	"\tfor x in $w\n"
	"\t\tif test $skip -eq 1\n"
	"\t\t\tset skip 0\n"
	"\t\t\tcontinue\n"
	"\t\tend\n"
	"\t\tif test $dd -eq 0\n"
	"\t\t\tswitch \"_$x\"\n"
	"\t\t\tcase '_--'\n"
	"\t\t\t\tset dd 1\n"
	"\t\t\t\tcontinue\n"
	"\t\t\tcase '_--*=*'\n"
	"\t\t\t\tcontinue\n"
	"\t\t\tcase '_--*'\n"
	"\t\t\t\t__~_vopt $s $x; and set skip 1\n"
	"\t\t\t\tcontinue\n"
	"\t\t\tcase '_-?*'\n"
	"\t\t\t\tset -l c (string split '' -- (string sub -s 2 -- $x))\n"
	"\t\t\t\tfor j in (seq (count $c))\n"
	"\t\t\t\t\tif __~_vopt $s -$c[$j]\n"
	"\t\t\t\t\t\ttest $j -eq (count $c); and set skip 1\n"
	"\t\t\t\t\t\tbreak\n"
	"\t\t\t\t\tend\n"
	"\t\t\t\tend\n"
	"\t\t\t\tcontinue\n"
	"\t\t\tend\n"
	"\t\tend\n"
	"\t\tif test $np -eq 0\n"
	"\t\t\tswitch \"$s:$x\"\n";

static const char fish_tail[] =
	"\t\t\tend\n"
	"\t\tend\n"
	"\t\tset np (math $np + 1)\n"
	"\tend\n"
	"\techo $s\n"
	"\techo $np\n"
	"end\n"
	"\n"
	"# __~_at NODE [MIN [MAX]]: is the cursor in NODE, after MIN to MAX\n"
	"# positional words?\n"
	"function __~_at\n"
	"\tset -l st (__~_state)\n"
	"\ttest \"$st[1]\" = \"$argv[1]\"; or return 1\n"
	"\tset -q argv[2]; or return 0\n"
	"\ttest $st[2] -ge $argv[2]; or return 1\n"
	"\tset -q argv[3]; or return 0\n"
	"\ttest $st[2] -le $argv[3]\n"
	"end\n"
	"\n";

#define NO_POS ((size_t)-1)

/* complete -c 'prog' -n '__~_at NODE [MIN [MAX]]' */
static void fish_line(struct gen *g, size_t min, size_t max)
{
	put_s(g, "complete -c ");
	put_qw(g, g->prog, 0);
	put_t(g, " -n '__~_at ");
	put_u(g, g->id);
	if (min != NO_POS) {
		put_c(g, ' ');
		put_u(g, (unsigned)min);
	}
	if (max != NO_POS) {
		put_c(g, ' ');
		put_u(g, (unsigned)max);
	}
	put_c(g, '\'');
}

static void fish_desc(struct gen *g, const char *help)
{
	if (!help || !*help)
		return;
	put_s(g, " -d ");
	put_qw(g, help, Q_LINE);
}

/* Value flags for an option (opt) or a positional. */
static void fish_act(struct gen *g, const struct act *a, bool opt)
{
	static const char *const fn[] = { "", "", "__fish_complete_directories",
					   "__fish_print_hostnames",
					   "__fish_complete_users" };

	if (a->kind == A_FILE) {
		put_s(g, opt ? " -r" : "");
		return;
	}
	put_s(g, opt ? " -x" : " -f");
	if (a->kind == A_WORDS) {
		put_s(g, " -a '");
		put_words(g, a, 0, false);
		put_c(g, '\'');
	} else if (a->kind != A_NONE) {
		put_s(g, " -a '(");
		put_s(g, fn[a->kind]);
		put_s(g, ")'");
	}
}

static void fish_opt(struct gen *g, const arg_item *it)
{
	struct form f[4];
	size_t i, n = get_forms(it, f);
	struct act a = value_act(it);
	char s[2];

	for (i = 0; i < n; i++) {
		fish_line(g, NO_POS, NO_POS);
		if (f[i].c) {
			s[0] = f[i].c;
			s[1] = '\0';
			put_s(g, " -s ");
			put_qw(g, s, 0);
			/* the long name joins the short one */
			if (i + 1 < n && !f[i + 1].c && !f[i + 1].neg &&
			    f[i + 1].name == it->name)
				i++;
		}
		if (!f[i].c) {
			put_s(g, " -l ");
			put_s(g, f[i].neg ? "'no-" : "'");
			put_q(g, f[i].name, 0);
			put_c(g, '\'');
		}
		if (val_mode(it) == 1 && !f[i].neg)
			fish_act(g, &a, true);
		fish_desc(g, it->help);
		put_c(g, '\n');
	}
}

static void fish_vopt_opt(struct gen *g, const arg_item *it)
{
	struct form f[4];
	size_t i, n = get_forms(it, f);
	bool first = true;

	if (val_mode(it) != 1)
		return;
	for (i = 0; i < n; i++) {
		put_s(g, first ? "\tcase " : " ");
		first = false;
		bash_key(g, &f[i], NULL);
	}
	if (!first)
		put_s(g, "\n\t\treturn 0\n");
}

static void fish_vopt(struct gen *g)
{
	each_opt(g, fish_vopt_opt);
}

static void fish_trans(struct gen *g)
{
	struct kid_it k;
	struct arg__iter it;
	const arg_item *x;
	const char *alias;

	kid_init(&k, g);
	while ((x = kid_next(&k))) {
		put_s(g, "\t\t\tcase ");
		bash_key(g, NULL, x->name);
		arg__iter_init(&it, x);
		while ((alias = alias_next(&it))) {
			put_c(g, ' ');
			bash_key(g, NULL, alias);
		}
		put_s(g, "\n\t\t\t\tset s ");
		put_u(g, k.id);
		put_s(g, "\n\t\t\t\tcontinue\n");
	}
}

static void fish_node(struct gen *g)
{
	struct kid_it k;
	struct arg__iter it;
	const arg_item *x;
	const char *alias;
	struct act a;
	size_t i;

	put_s(g, "\n");
	each_opt(g, fish_opt);
	kid_init(&k, g);
	while ((x = kid_next(&k))) {
		arg__iter_init(&it, x);
		alias = x->name;
		do {
			fish_line(g, 0, 0);
			put_s(g, " -a ");
			put_qw(g, alias, 0);
			fish_desc(g, x->help);
			put_c(g, '\n');
		} while ((alias = alias_next(&it)));
	}
	/* positionals: files are fish's default, everything else says -f */
	for (i = 0; (x = positional(g, i)); i++) {
		a = value_act(x);
		if (a.kind != A_FILE) {
			fish_line(g, i, x->many ? NO_POS : i);
			fish_act(g, &a, false);
			put_c(g, '\n');
		}
		if (x->many)
			return;
	}
	/* no more positionals */
	fish_line(g, i, NO_POS);
	put_s(g, " -f\n");
}

static void gen_fish(struct gen *g)
{
	put_s(g, "# fish completion for ");
	put_s(g, g->prog);
	put_s(g, "\n# Generated by Better-arg; do not edit.\n\n");
	put_t(g, fish_head);
	walk(g, 0, 0, fish_vopt);
	put_t(g, fish_state);
	walk(g, 0, 0, fish_trans);
	put_t(g, fish_tail);
	put_s(g, "# options, subcommands and arguments per node");
	walk(g, 0, 0, fish_node);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                        */
/* ------------------------------------------------------------------ */

int arg_completion(arg *p, unsigned shell, arg_write_fn write, void *ud)
{
	struct gen g;

	if (!p || !write)
		return -1;
	if (shell != ARG_SH_BASH && shell != ARG_SH_ZSH &&
	    shell != ARG_SH_FISH)
		return -1;
	memset(&g, 0, sizeof(g));
	g.v = arg__cpriv(p);
	if (!g.v->root || g.v->root->kind != ARG_I_CMD ||
	    !safe(g.v->root->name))
		return -1;
	g.w = write;
	g.ud = ud;
	g.fish = shell == ARG_SH_FISH;
	g.prog = g.v->root->name;
	g.path[0] = g.v->root;
	if (shell == ARG_SH_BASH)
		gen_bash(&g);
	else if (shell == ARG_SH_ZSH)
		gen_zsh(&g);
	else
		gen_fish(&g);
	flush(&g);
	return g.err;
}
