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

#endif /* ARG_INTERNAL_H */
