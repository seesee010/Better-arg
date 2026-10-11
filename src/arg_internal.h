/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arg_internal.h - private interfaces shared between the modules
 *
 * Nothing in here is installed. Symbols use the arg__ prefix. Each
 * section names the source file that owns (implements) it; other
 * modules may only call what is declared here.
 */
#ifndef ARG_INTERNAL_H
#define ARG_INTERNAL_H

#include "arg.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Assertions and exit (SDD 8.5, 10.3)                                */
/* ------------------------------------------------------------------ */

#ifndef ARG_ASSERT
#if defined(NDEBUG) || defined(ARG_FREESTANDING)
#define ARG_ASSERT(x) ((void)0)
#else
#include <assert.h>
#define ARG_ASSERT(x) assert(x)
#endif
#endif

#ifndef ARG_EXIT
#ifdef ARG_FREESTANDING
#define ARG_EXIT(code) ((void)(code))
#else
#include <stdlib.h>
#define ARG_EXIT(code) exit(code)
#endif
#endif

#define ARG__COUNTOF(a) (sizeof(a) / sizeof((a)[0]))

/* ------------------------------------------------------------------ */
/* hosted.c - default hooks for hosted builds                         */
/* ------------------------------------------------------------------ */

int arg__write_stdout(void *ud, const char *s, size_t n);
int arg__write_stderr(void *ud, const char *s, size_t n);
const char *arg__getenv_default(void *ud, const char *name);
void arg__exit_default(void *ud, int code);

/* ------------------------------------------------------------------ */
/* types.c - value conversion helpers                                 */
/* ------------------------------------------------------------------ */

/*
 * Read a stored value of type t (kind INT / UINT / DOUBLE / BOOL / ENUM)
 * and widen it. Return false when the kind does not fit the request.
 */
bool arg__load_int(const arg_type *t, const void *src, int64_t *out);
bool arg__load_uint(const arg_type *t, const void *src, uint64_t *out);
bool arg__load_double(const arg_type *t, const void *src, double *out);

/*
 * Compare two stored values of type t: *cmp < 0, 0, > 0. Returns false
 * when the type is not numeric (then .min/.max are a spec error).
 */
bool arg__compare(const arg_type *t, const void *a, const void *b, int *cmp);

/* ASCII-only helpers, never locale dependent. */
bool arg__ieq(const char *a, size_t an, const char *b); /* b NUL-terminated */

/* ------------------------------------------------------------------ */
/* suggest.c - "did you mean" (SDD 8.5, 15)                           */
/* ------------------------------------------------------------------ */

#define ARG__SUGGEST_MAX_LEN 64

/*
 * Bounded optimal-string-alignment (Damerau-Levenshtein) distance.
 * Returns a value > max as soon as the distance must exceed max, and
 * for strings longer than ARG__SUGGEST_MAX_LEN. No heap.
 */
unsigned arg__distance(const char *a, size_t an, const char *b, size_t bn,
		       unsigned max);

struct arg__suggester {
	const char *input;
	size_t input_len;
	arg_suggest_fn metric; /* never NULL after init           */
	unsigned limit;        /* largest accepted distance       */
	unsigned best_dist;
	const char *best;      /* NULL until a candidate fits     */
};

/* metric NULL selects arg__distance; limit derives from input length. */
void arg__suggest_init(struct arg__suggester *s, const char *input, size_t n,
		       arg_suggest_fn metric);
/* Offer a NUL-terminated candidate; the first of equal distance wins. */
void arg__suggest_add(struct arg__suggester *s, const char *cand);
const char *arg__suggest_best(const struct arg__suggester *s);

/* ------------------------------------------------------------------ */
/* error.c - messages and error output (SDD 12)                       */
/* ------------------------------------------------------------------ */

/* Command path for messages: "prog repo add". */
struct arg__path {
	const char *prog;            /* argv[0] basename, NULL = root name */
	const arg_item *const *cmds; /* cmds[0] is the root                */
	size_t depth;                /* number of entries in cmds          */
};

/* One substitution argument for arg__format(). */
struct arg__str {
	const char *s;
	size_t n;
	bool untrusted; /* user input: non-printables shown as \xNN */
};

/* Message text: m->text[id] when set, else the default table. */
const char *arg__msg(const arg_messages *m, arg_msg_id id);

/*
 * Write fmt replacing each %s with the next argument and %% with '%'.
 * Missing arguments expand to nothing. Never interprets user text.
 */
int arg__format(arg_write_fn w, void *ud, const char *fmt,
		const struct arg__str *args, size_t nargs);
/* Write s[0..n) with bytes < 0x20, 0x7f and invalid UTF-8 as \xNN. */
int arg__write_escaped(arg_write_fn w, void *ud, const char *s, size_t n);
/* "prog repo add" */
int arg__write_path(arg_write_fn w, void *ud, const struct arg__path *path);
/*
 * "--jobs", "-j" or "<url>"; with_metavar adds " <N>" for value
 * options (metavar from .value, else type->name, else "VALUE").
 */
int arg__write_item_label(arg_write_fn w, void *ud, const arg_item *it,
			  bool with_metavar);
/*
 * Full error report as in SDD 12.2:
 *   prog: <message>[: <detail>]
 *     Did you mean '--jobs'?
 *   Try 'prog --help' for more information.
 */
int arg__write_error(arg_write_fn w, void *ud, const arg_error *e,
		     const struct arg__path *path, const arg_messages *m);

/* ------------------------------------------------------------------ */
/* parse.c - parser core (SDD 5, 7, 9, 13)                            */
/*                                                                    */
/* Implemented in parse.c (main pass, report, run), spec.c (spec      */
/* compiler, per-command index, tree walks), store.c (arena, value    */
/* storage) and getters.c. rules.c, help.c and complete.c use only    */
/* what is declared here.                                             */
/* ------------------------------------------------------------------ */

/* ARG_USE nesting limit inside one command; deeper is a spec error. */
#define ARG__USE_MAX 8

/*
 * Children of a command in declaration order with ARG_USE spliced in
 * transparently: never yields ARG_I_USE or ARG_I_END items.
 *
 *	struct arg__iter it;
 *	const arg_item *x;
 *
 *	arg__iter_init(&it, cmd);
 *	while ((x = arg__iter_next(&it)))
 *		...
 *
 * A broken ARG_USE (NULL tree, nesting deeper than ARG__USE_MAX) is
 * skipped and recorded in error / error_item. The iterator holds no
 * pointers into itself, so a struct copy is an independent iterator.
 */
struct arg__iter {
	const arg_item *stack[ARG__USE_MAX + 1];
	size_t sp;
	const char *error;          /* static text, NULL = fine */
	const arg_item *error_item; /* the offending ARG_USE    */
};

void arg__iter_init(struct arg__iter *it, const arg_item *cmd);
const arg_item *arg__iter_next(struct arg__iter *it);

/* One chunk of a list value (.many options, ARG_ARGS). */
struct arg__chunk {
	struct arg__chunk *next;
	unsigned char *data; /* cap elements of the value type's size */
	size_t n, cap;
};

/*
 * Run-time state of one option or positional, owned by the index of
 * the command that declares it (a .global option has one state, in
 * its own command, whichever depth it was given at).
 */
struct arg__state {
	const arg_item *item;
	union {
		void *one;               /* single value; NULL = none  */
		struct arg__chunk *list; /* .many values; NULL = none  */
	} v;
	uint32_t count; /* CLI occurrences; for ARG_I_COUNT the level */
	uint8_t source; /* arg_source_kind                            */
};

/* Long-name lookup entry; the name is derived from st and form. */
struct arg__long {
	uint16_t st;   /* index into arg__cmd.st (an option)         */
	uint16_t len;  /* cached name length                         */
	uint8_t form;  /* ARG__LONG_NAME, _ALIAS or _NEG ("no-" name) */
};

#define ARG__LONG_NAME 0
#define ARG__LONG_ALIAS 1
#define ARG__LONG_NEG 2

/* Short table value meaning "search st[] linearly" (index >= 254). */
#define ARG__SHORT_FAR 255

/*
 * Compiled index of one command, built lazily the first time the
 * command is entered (and checked by the spec compiler on the way).
 * Options and positionals are kept in declaration order with ARG_USE
 * spliced; everything else (subcommands, aliases, groups, rules,
 * examples) is read from the spec with arg__iter.
 */
struct arg__cmd {
	const arg_item *cmd;
	struct arg__cmd *parent; /* NULL for the root                */
	unsigned level;          /* 0 = root                         */
	struct arg__state *st;   /* nopt options, then npos positionals */
	size_t nopt, npos;
	size_t nsub;             /* number of ARG_I_CMD children     */
	const arg_item *main;    /* ARG_I_MAIN child or NULL         */
	struct arg__long *longs; /* sorted by (len, bytes)           */
	size_t nlong;
	uint8_t *shorts;         /* [128]: index + 1, 0 = none; NULL */
	bool digit_short;        /* has a digit short option         */
	bool digit_global;       /* ... that is also .global         */
	/* main pass state */
	size_t pos_next;         /* next positional slot             */
	size_t pos_given;        /* positional values received       */
};

/* The arena (SDD 13.1); blocks come from cfg.alloc. */
struct arg__block {
	struct arg__block *next;
	size_t size; /* bytes, including this header */
};

struct arg__arena {
	unsigned char *buf;
	size_t cap, used;
	struct arg__block *blocks; /* newest first */
};

/* Path storage for any command: path.cmds points into cmds. */
struct arg__pathbuf {
	const arg_item *cmds[ARG_MAX_DEPTH + 1];
	struct arg__path path;
};

/*
 * Private parser state, stored in arg_parser._p (size checked at
 * compile time in parse.c). Read it through arg__priv(); the fields
 * are documented for rules.c, help.c and complete.c, which may read
 * them but should change state only through the functions below.
 */
struct arg__priv {
	arg_config cfg;          /* copy; NULL hooks replaced by defaults */
	const arg_item *root;    /* items[0] given to arg_init()          */
	int exit_on_error;
	arg_status status;       /* result of the last arg_parse()        */
	arg_error err;           /* current error, status ARG_OK if none  */
	struct arg__arena arena;
	/* selected path; idx[i] is the index of cmds[i], depth entries */
	const arg_item *cmds[ARG_MAX_DEPTH + 1];
	struct arg__cmd *idx[ARG_MAX_DEPTH + 1];
	size_t depth;            /* 0 before arg_parse(), root = 1        */
	const char *prog;        /* argv[0] basename, NULL = root name    */
	int argc;                /* argv entries before the first NULL    */
	char *const *argv;
	const arg_item *do_item; /* first ARG_DO given, or NULL           */
	const char *fail_msg;    /* pending arg_fail() message            */
	struct arg__chunk *rest; /* ARG_ALLOW_UNKNOWN arguments           */
	int unknown_argi;        /* argv index for on_unknown_cmd, or -1  */
	const char *path_str;    /* arg_path() cache                      */
	char short_text[3];      /* "-x" for unknown short options        */
	bool diag_done;          /* getter misuse reported already        */
};

struct arg__priv *arg__priv(arg *p);
const struct arg__priv *arg__cpriv(const arg *p);

/* ---- memory (store.c) ---- */

/* Aligned arena memory, NULL when arena and cfg.alloc are exhausted. */
void *arg__alloc(arg *p, size_t n);
/* Start over with the initial arena; frees every cfg.alloc block. */
void arg__arena_reset(arg *p);
/* Remember the arena position / drop everything allocated since. */
void arg__arena_mark(arg *p, struct arg__arena *m);
void arg__arena_release(arg *p, const struct arg__arena *m);

/* ---- items and values (store.c) ---- */

/*
 * The type values of an item are stored and converted with: FLAG ->
 * its type (bool), COUNT -> arg_t_u32 (the level), VALUE / ARG -> its
 * type or arg_t_string. NULL for every other kind.
 */
const arg_type *arg__value_type(const arg_item *it);
/* Lookup key (SDD 5.3); buf receives "-x" for short-only options. */
const char *arg__item_key(const arg_item *it, char buf[3]);
/* Number of values: list length, 1 or 0 for single values. */
size_t arg__state_len(const struct arg__state *s);
/* Pointer to value i in arg__value_type() layout, NULL if none. */
const void *arg__state_value(const struct arg__state *s, size_t i);
/* true when the source counts as "set" per cfg.rule_sources (SDD 10.2) */
bool arg__state_is_set(const arg *p, const struct arg__state *s);
/*
 * Convert text[0..n) (text[n] must be '\0') with the item's type and
 * store it: replaces a single value, appends to a list, splits on
 * .sep for lists. Does not touch source or count. On failure sets the
 * error (argi as given) and returns its status.
 */
arg_status arg__store_text(arg *p, struct arg__state *s, const char *text,
			   size_t n, int argi);
/* Append a pointer to the ARG_ALLOW_UNKNOWN list. */
arg_status arg__rest_push(arg *p, const char *s);

/* ---- spec compiler and index (spec.c) ---- */

/*
 * Build (and check) the index of cmd, a child of parent (NULL for the
 * root). On a spec error sets the error and returns ARG_E_SPEC.
 */
arg_status arg__index(arg *p, const arg_item *cmd, struct arg__cmd *parent,
		      struct arg__cmd **out);
/* Option of c (not its ancestors) by long name, alias or "-x"; or NULL. */
struct arg__state *arg__find_option(struct arg__cmd *c, const char *name);
/* Option of c by short char / by long name s[0..n); *neg for "no-". */
struct arg__state *arg__short_lookup(struct arg__cmd *c, char ch);
struct arg__state *arg__long_lookup(struct arg__cmd *c, const char *s,
				    size_t n, bool *neg);
/* Subcommand of cmd named s[0..n) by name or ARG_ALIAS, or NULL. */
const arg_item *arg__sub_lookup(const arg_item *cmd, const char *s,
				size_t n);
/*
 * Command at path below the root ("repo add", "" = root, NULL = the
 * selected command); fills out (prog included). NULL if not found.
 */
const arg_item *arg__find_cmd(arg *p, const char *path,
			      struct arg__pathbuf *out);
/* Path from the root to any command of the tree; false if not in it. */
bool arg__cmd_path(arg *p, const arg_item *cmd, struct arg__pathbuf *out);
/* State of item on the selected path, or NULL (not on the path). */
struct arg__state *arg__state_of(arg *p, const arg_item *item);

/* ---- errors and helpers for rules / checks (parse.c) ---- */

/*
 * Record an error tied to no argv position (argi -1) for the deepest
 * selected command. Returns st.
 */
arg_status arg__set_error(arg *p, arg_status st, const arg_item *item,
			  const char *detail);
/* Take (and clear) the message stored by arg_fail(). */
const char *arg__take_fail(arg *p);
/* The selected path for messages. */
void arg__selected_path(const arg *p, struct arg__path *out);

/* ---- hooks into later modules ---- */

/* rules.c: every ARG_I_RULE item of cmd, at index build; ARG_E_SPEC. */
arg_status arg__rules_compile(const arg_item *cmd, const arg_item *rule,
			      arg_error *err);
/*
 * rules.c: after values, bindings and required checks, before .check
 * functions; skipped when an ARG_DO fired. Uses arg__set_error().
 */
arg_status arg__rules_check(arg *p);
/* help.c: built-in help for cmd (any command of the tree). */
int arg__render_help(arg *p, const arg_item *cmd, arg_write_fn w, void *ud);

#endif /* ARG_INTERNAL_H */
