/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arg.h - Better-arg, a C99 command line argument parsing library
 *
 * One declaration table and one function call turn argv into typed,
 * validated values. Every stage (tokenizing, conversion, validation,
 * help, errors, memory) can be replaced through a hook.
 *
 * Design reference: docs/SDD.md. The SDD calls the library "argot";
 * this implementation uses the prefix arg_ / ARG_ instead.
 *
 * Layers:
 *   Level 0  ARG_RUN(...)             declare, parse, dispatch
 *   Level 1  arg_item arrays          arg_init(), arg_parse(), arg_run()
 *   Level 2  arg_tokenizer            syntax only, no spec
 *
 * Symbols starting with arg__ / ARG__ are private.
 */
#ifndef ARG_H
#define ARG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Version                                                            */
/* ------------------------------------------------------------------ */

#define ARG_VERSION_MAJOR 0
#define ARG_VERSION_MINOR 1
#define ARG_VERSION_PATCH 0
#define ARG_VERSION_STRING "0.1.0"

/* Run-time version, "MAJOR.MINOR.PATCH". */
const char *arg_version(void);

/* ------------------------------------------------------------------ */
/* Compile-time configuration (SDD 16.2)                              */
/* ------------------------------------------------------------------ */

/*
 * ARG_EXIT_ON_ERROR 1 (default): on any non-OK status arg_parse()
 * prints help, version or the error and ends the program.
 * ARG_EXIT_ON_ERROR 0: the status is returned to the caller.
 *
 * The value is captured per translation unit by arg_init() and
 * ARG_RUN(), so defining it before including this header is enough.
 */
#ifndef ARG_EXIT_ON_ERROR
#ifdef ARG_FREESTANDING
#define ARG_EXIT_ON_ERROR 0
#else
#define ARG_EXIT_ON_ERROR 1
#endif
#endif

#ifndef ARG_INLINE_ARENA
#define ARG_INLINE_ARENA 1024 /* bytes inside every arg_parser */
#endif

#ifndef ARG_MAX_DEPTH
#define ARG_MAX_DEPTH 16 /* maximum subcommand nesting */
#endif

#ifndef ARG_MAX_SOURCES
#define ARG_MAX_SOURCES 4 /* extra value-source hook slots */
#endif

/* ------------------------------------------------------------------ */
/* Forward declarations                                               */
/* ------------------------------------------------------------------ */

typedef struct arg_parser arg_parser;
typedef struct arg_parser arg; /* what handlers receive */
typedef struct arg_item arg_item;
typedef struct arg_type arg_type;
typedef struct arg_ctx arg_ctx;
typedef struct arg_config arg_config;
typedef struct arg_error arg_error;
typedef struct arg_messages arg_messages;

/* ------------------------------------------------------------------ */
/* Status codes (SDD 12.1)                                            */
/* ------------------------------------------------------------------ */

typedef enum arg_status {
	ARG_OK = 0,
	ARG_EXIT_HELP,            /* help requested, exit code 0          */
	ARG_EXIT_VERSION,         /* version requested, exit code 0       */
	ARG_E_UNKNOWN_OPTION,     /* no such option                       */
	ARG_E_AMBIGUOUS,          /* prefix matches several options       */
	ARG_E_MISSING_VALUE,      /* option needs a value                 */
	ARG_E_UNEXPECTED_VALUE,   /* flag got =value                      */
	ARG_E_BAD_VALUE,          /* type conversion or check failed      */
	ARG_E_RANGE,              /* value outside allowed range          */
	ARG_E_REPEATED,           /* .once option given twice             */
	ARG_E_MISSING_REQUIRED,   /* required option absent               */
	ARG_E_TOO_MANY_ARGS,      /* too many positionals                 */
	ARG_E_TOO_FEW_ARGS,       /* too few positionals                  */
	ARG_E_UNKNOWN_COMMAND,    /* no such subcommand                   */
	ARG_E_MISSING_COMMAND,    /* grouping command without subcommand  */
	ARG_E_RULE,               /* a command rule failed                */
	ARG_E_NO_MEMORY,          /* arena and alloc hook exhausted       */
	ARG_E_IO,                 /* response file unreadable             */
	ARG_E_SPEC,               /* programmer error in the spec         */
	ARG_E_ACTION,             /* an ARG_ON function returned non-zero */
	ARG_STATUS_COUNT
} arg_status;

/* "ARG_E_RANGE" etc.; never NULL. */
const char *arg_status_name(arg_status st);

/*
 * Process exit code for a status: 0 for OK / help / version, 2 for
 * usage errors, 66 for I/O, 70 for memory and spec errors, 1 for
 * ARG_E_ACTION.
 */
int arg_exit_code(arg_status st);

/* ------------------------------------------------------------------ */
/* Callback types                                                     */
/* ------------------------------------------------------------------ */

/* Command handler and ARG_DO function; the return value is the exit code. */
typedef int (*arg_run_fn)(arg *a);
typedef int (*arg_do_fn)(arg *a);
/* ARG_ON function; value is NULL for ARG_ON items without .value. */
typedef int (*arg_on_fn)(arg *a, const char *value);
/* ARG_RULE_FN; rule is the rule item (rule->udata holds the user data). */
typedef int (*arg_rule_fn)(arg *a, const arg_item *rule);
/* .check function; key is the item's lookup key for the getters. */
typedef int (*arg_check_fn)(arg *a, const char *key);

/* Output sink: write n bytes, return 0 on success. */
typedef int (*arg_write_fn)(void *ud, const char *s, size_t n);
/* Environment lookup, NULL when unset. */
typedef const char *(*arg_getenv_fn)(void *ud, const char *name);
/*
 * realloc-style allocator: new_size == 0 frees ptr and returns NULL.
 * old_size is the size of the previous block (0 for ptr == NULL).
 */
typedef void *(*arg_alloc_fn)(void *ud, void *ptr, size_t old_size,
			      size_t new_size);
/*
 * Extra value layer (config file, registry, ...): return the text value
 * for item inside command cmd, or NULL when the source has none.
 */
typedef const char *(*arg_source_fn)(void *ud, const arg_item *cmd,
				     const arg_item *item, const char *key);
/* Replaces error printing; returns the exit code to use. */
typedef int (*arg_error_fn)(arg *a, const arg_error *e);
/* Replaces the help renderer for command cmd; returns 0 on success. */
typedef int (*arg_help_fn)(arg *a, const arg_item *cmd, arg_write_fn write,
			   void *ud);
/*
 * Replaces the distance metric used for "did you mean" suggestions.
 * Returns the distance between a and b, or any value > max when it
 * exceeds max.
 */
typedef unsigned (*arg_suggest_fn)(const char *a, size_t an, const char *b,
				   size_t bn, unsigned max);
/*
 * Claims an unknown subcommand name (git-style external commands).
 * argv[0] is the unknown name, the rest are the remaining arguments.
 * Return the exit code; it is called from arg_run().
 */
typedef int (*arg_unknown_fn)(arg *a, int argc, char *const *argv);
/* Terminal width in columns, 0 = unknown. */
typedef unsigned (*arg_width_fn)(void *ud);
/* Ends the program; must not return. */
typedef void (*arg_exit_fn)(void *ud, int code);
/* Completion candidate sink. */
typedef int (*arg_emit_fn)(void *ud, const char *s, size_t n);

/* ------------------------------------------------------------------ */
/* Types (SDD 8)                                                      */
/* ------------------------------------------------------------------ */

/* Storage class of a type, tells the getters how to read a value. */
typedef enum arg_kind {
	ARG_K_CUSTOM = 0, /* opaque, read with arg_get()              */
	ARG_K_BOOL,       /* bool                                     */
	ARG_K_INT,        /* signed integer, .size = 1, 2, 4 or 8     */
	ARG_K_UINT,       /* unsigned integer, .size = 1, 2, 4 or 8   */
	ARG_K_DOUBLE,     /* double (.size 8) or float (.size 4)      */
	ARG_K_STR,        /* const char *                             */
	ARG_K_ENUM        /* int index into the item's choices        */
} arg_kind;

/* Hints for help and completion. */
#define ARG_HINT_FILE 0x01u
#define ARG_HINT_DIR 0x02u
#define ARG_HINT_HOST 0x04u
#define ARG_HINT_USER 0x08u

struct arg_type {
	const char *name; /* metavar default, e.g. "N", "FILE"        */
	size_t size;      /* bytes per element                         */
	/*
	 * Convert s[0..n) into *dst. s is not necessarily NUL-terminated.
	 * Return 0 on success, otherwise the result of arg_reject().
	 */
	int (*parse)(const arg_type *t, const char *s, size_t n, void *dst,
		     arg_ctx *ctx);
	/*
	 * Optional: write src as text into buf (always NUL-terminated when
	 * cap > 0). Returns the full length, or -1 on error.
	 */
	int (*format)(const arg_type *t, const void *src, char *buf,
		      size_t cap);
	/* Optional: emit completion candidates starting with prefix. */
	int (*complete)(const arg_type *t, const char *prefix,
			arg_emit_fn emit, void *ud);
	const void *ud; /* type data: range, inner type, choices       */
	uint32_t hints; /* ARG_HINT_*                                  */
	arg_kind kind;  /* storage class                               */
};

/* Conversion context passed to arg_type.parse. */
struct arg_ctx {
	arg *parser;          /* NULL when called via arg_type_parse() */
	const arg_item *item; /* item being converted, may be NULL     */
	const char *detail;   /* set by arg_reject()                   */
	arg_status status;    /* ARG_E_BAD_VALUE or ARG_E_RANGE        */
};

/*
 * Reject a value from inside a type's parse function. msg must stay
 * valid (use a string literal). Returns -1.
 */
int arg_reject(arg_ctx *ctx, const char *msg);
/* Same, with an explicit status (ARG_E_RANGE for bounds). */
int arg_reject_status(arg_ctx *ctx, arg_status st, const char *msg);

/*
 * Run a type's converter outside the parser. text must be
 * NUL-terminated. On failure *detail (if not NULL) receives the
 * rejection message. Enum types need an item and fail here.
 */
arg_status arg_type_parse(const arg_type *t, const char *text, void *dst,
			  const char **detail);

/* Value of a hex digit, -1 if c is not one. */
int arg_hexval(char c);

/* Built-in types (SDD 8.2). */
extern const arg_type arg_t_bool;     /* bool                         */
extern const arg_type arg_t_i8;       /* int8_t                       */
extern const arg_type arg_t_i16;      /* int16_t                      */
extern const arg_type arg_t_i32;      /* int32_t                      */
extern const arg_type arg_t_i64;      /* int64_t                      */
extern const arg_type arg_t_int;      /* int                          */
extern const arg_type arg_t_long;     /* long                         */
extern const arg_type arg_t_u8;       /* uint8_t                      */
extern const arg_type arg_t_u16;      /* uint16_t                     */
extern const arg_type arg_t_u32;      /* uint32_t                     */
extern const arg_type arg_t_u64;      /* uint64_t                     */
extern const arg_type arg_t_uint;     /* unsigned int                 */
extern const arg_type arg_t_size_t;   /* size_t                       */
extern const arg_type arg_t_double;   /* double                       */
extern const arg_type arg_t_float;    /* float                        */
extern const arg_type arg_t_string;   /* const char *                 */
extern const arg_type arg_t_enum;     /* int, choices from item.names */
extern const arg_type arg_t_bytesize; /* uint64_t bytes: 4k, 16M      */
extern const arg_type arg_t_duration; /* uint64_t ms: 1m30s           */

/* Decorator data (SDD 8.3). */
typedef union arg_num {
	int64_t i;
	uint64_t u;
	double d;
} arg_num;

typedef struct arg_range {
	const arg_type *inner;
	arg_num lo, hi; /* inclusive */
} arg_range;

typedef struct arg_wrap {
	const arg_type *inner;
	size_t n; /* ARG_MAXLEN limit */
} arg_wrap;

/* Decorator converters, usable in hand-written types too. */
int arg_range_parse(const arg_type *t, const char *s, size_t n, void *dst,
		    arg_ctx *ctx);
int arg_nonempty_parse(const arg_type *t, const char *s, size_t n, void *dst,
		       arg_ctx *ctx);
int arg_maxlen_parse(const arg_type *t, const char *s, size_t n, void *dst,
		     arg_ctx *ctx);
int arg_one_of_parse(const arg_type *t, const char *s, size_t n, void *dst,
		     arg_ctx *ctx);
/* Formats through ((const arg_range or arg_wrap *)t->ud)->inner. */
int arg_wrap_format(const arg_type *t, const void *src, char *buf,
		    size_t cap);

#define ARG_RANGE_INT(lo_, hi_)                                              \
	(&(const arg_type){ .name = "N", .size = sizeof(int64_t),            \
			    .parse = arg_range_parse,                        \
			    .format = arg_wrap_format,                       \
			    .ud = &(const arg_range){ .inner = &arg_t_i64,   \
						      .lo.i = (lo_),         \
						      .hi.i = (hi_) },       \
			    .kind = ARG_K_INT })
#define ARG_RANGE_UINT(lo_, hi_)                                             \
	(&(const arg_type){ .name = "N", .size = sizeof(uint64_t),           \
			    .parse = arg_range_parse,                        \
			    .format = arg_wrap_format,                       \
			    .ud = &(const arg_range){ .inner = &arg_t_u64,   \
						      .lo.u = (lo_),         \
						      .hi.u = (hi_) },       \
			    .kind = ARG_K_UINT })
#define ARG_RANGE_DOUBLE(lo_, hi_)                                           \
	(&(const arg_type){ .name = "NUM", .size = sizeof(double),           \
			    .parse = arg_range_parse,                        \
			    .format = arg_wrap_format,                       \
			    .ud = &(const arg_range){ .inner = &arg_t_double,\
						      .lo.d = (lo_),         \
						      .hi.d = (hi_) },       \
			    .kind = ARG_K_DOUBLE })
/* String decorators; inner must be a string type. */
#define ARG_NONEMPTY(inner_)                                                 \
	(&(const arg_type){ .name = "TEXT", .size = sizeof(const char *),    \
			    .parse = arg_nonempty_parse,                     \
			    .format = arg_wrap_format,                       \
			    .ud = &(const arg_wrap){ .inner = (inner_) },    \
			    .kind = ARG_K_STR })
#define ARG_MAXLEN(inner_, n_)                                               \
	(&(const arg_type){ .name = "TEXT", .size = sizeof(const char *),    \
			    .parse = arg_maxlen_parse,                       \
			    .format = arg_wrap_format,                       \
			    .ud = &(const arg_wrap){ .inner = (inner_),      \
						     .n = (n_) },            \
			    .kind = ARG_K_STR })
/* A string that must be one of the given words; stored as const char *. */
#define ARG_ONE_OF_STR(...)                                                  \
	(&(const arg_type){ .name = "WORD", .size = sizeof(const char *),    \
			    .parse = arg_one_of_parse,                       \
			    .ud = ARG_CHOICES(__VA_ARGS__),                  \
			    .kind = ARG_K_STR })

/* NULL-terminated word list for ARG_ENUM and ARG_ONE_OF_STR. */
#define ARG_CHOICES(...) ((const char *const[]){ __VA_ARGS__, NULL })

/* ------------------------------------------------------------------ */
/* Optional variable binding (.to)                                    */
/* ------------------------------------------------------------------ */

typedef enum arg_to_type {
	ARG_TO_T_NONE = 0,
	ARG_TO_T_BOOL,   /* bool                                   */
	ARG_TO_T_INT,    /* int                                    */
	ARG_TO_T_LONG,   /* long                                   */
	ARG_TO_T_I64,    /* int64_t                                */
	ARG_TO_T_UINT,   /* unsigned int                           */
	ARG_TO_T_ULONG,  /* unsigned long                          */
	ARG_TO_T_U64,    /* uint64_t                               */
	ARG_TO_T_SIZE,   /* size_t                                 */
	ARG_TO_T_DOUBLE, /* double                                 */
	ARG_TO_T_FLOAT,  /* float                                  */
	ARG_TO_T_STR,    /* const char *                           */
	ARG_TO_T_RAW     /* type->size bytes, for custom types     */
} arg_to_type;

typedef struct arg_to {
	void *ptr;
	arg_to_type type;
} arg_to;

/*
 * C99 has no _Generic, so the variable type is part of the macro name.
 * A binding that does not fit the item's type is a spec error. Integer
 * bindings are range-checked (ARG_E_RANGE).
 */
/* (1 ? p : (T *)0) makes the compiler reject a pointer of another type */
#define ARG__TO(p, T, id) { .ptr = (1 ? (p) : (T *)0), .type = (id) }
#define ARG_TO_BOOL(p) ARG__TO(p, bool, ARG_TO_T_BOOL)
#define ARG_TO_INT(p) ARG__TO(p, int, ARG_TO_T_INT)
#define ARG_TO_LONG(p) ARG__TO(p, long, ARG_TO_T_LONG)
#define ARG_TO_I64(p) ARG__TO(p, int64_t, ARG_TO_T_I64)
#define ARG_TO_UINT(p) ARG__TO(p, unsigned, ARG_TO_T_UINT)
#define ARG_TO_ULONG(p) ARG__TO(p, unsigned long, ARG_TO_T_ULONG)
#define ARG_TO_U64(p) ARG__TO(p, uint64_t, ARG_TO_T_U64)
#define ARG_TO_SIZE(p) ARG__TO(p, size_t, ARG_TO_T_SIZE)
#define ARG_TO_DOUBLE(p) ARG__TO(p, double, ARG_TO_T_DOUBLE)
#define ARG_TO_FLOAT(p) ARG__TO(p, float, ARG_TO_T_FLOAT)
#define ARG_TO_STR(p) ARG__TO(p, const char *, ARG_TO_T_STR)
#define ARG_TO_RAW(p) { .ptr = (p), .type = ARG_TO_T_RAW }

/* ------------------------------------------------------------------ */
/* Items (SDD 5.1)                                                    */
/* ------------------------------------------------------------------ */

typedef enum arg_item_kind {
	ARG_I_END = 0, /* terminates every item list                */
	ARG_I_FLAG,    /* bool, no value                            */
	ARG_I_COUNT,   /* +1 per occurrence (-vvv)                  */
	ARG_I_VALUE,   /* typed value; .many = true collects a list */
	ARG_I_DO,      /* flag that runs a function, then exits     */
	ARG_I_ON,      /* runs a function on every occurrence       */
	ARG_I_ARG,     /* positional                                */
	ARG_I_CMD,     /* subcommand: holds child items             */
	ARG_I_ALIAS,   /* extra name for the enclosing command      */
	ARG_I_GROUP,   /* starts a help section                     */
	ARG_I_RULE,    /* mutex, requires, one-of, custom ...       */
	ARG_I_EXAMPLE, /* example line for --help                   */
	ARG_I_USE,     /* splices in a tree defined elsewhere       */
	ARG_I_MAIN     /* handler of the enclosing command          */
} arg_item_kind;

typedef enum arg_rule_kind {
	ARG_R_NONE = 0,
	ARG_R_MUTEX,     /* at most one of names                  */
	ARG_R_ONE_OF,    /* exactly one of names                  */
	ARG_R_ANY_OF,    /* at least one of names                 */
	ARG_R_REQUIRES,  /* names[0] set => names[1..] all set    */
	ARG_R_CONFLICTS, /* names[0] set => none of names[1..]    */
	ARG_R_CUSTOM     /* fn.rule decides                       */
} arg_rule_kind;

struct arg_item {
	/* set by the macro's fixed arguments */
	arg_item_kind kind;
	char short_name;      /* 0 = none                                */
	const char *name;     /* long / positional / command name        */
	const char *help;     /* help text; GROUP title; EXAMPLE text    */
	const arg_type *type; /* NULL = derived from kind (bool, string) */
	union {
		arg_run_fn run;     /* CMD, MAIN                  */
		arg_do_fn on_do;    /* DO                         */
		arg_on_fn on_each;  /* ON                         */
		arg_rule_fn rule;   /* RULE (ARG_R_CUSTOM)        */
	} fn;
	const arg_item *items;    /* CMD children / USE target            */
	const char *const *names; /* RULE members / ENUM choices, NULL-end */
	arg_rule_kind rule_kind;

	/* optional fields: zero / NULL always means "off" */
	const char *def, *min, *max; /* text, parsed by .type           */
	const char *env;             /* env var fallback                */
	const char *value;           /* metavar shown in help: "DIR"    */
	const char *id;              /* lookup key override             */
	const char *alias;           /* extra long name for an option   */
	const char *deprecated;      /* "use --output": works, warns    */
	size_t max_count;            /* ARGS: 0 = unlimited             */
	char sep;                    /* split "a,b" on this char        */
	unsigned global : 1, hidden : 1, required : 1, many : 1, once : 1,
		negatable : 1, optional_value : 1, check_on_do : 1;
	arg_check_fn check; /* per-item validator             */
	arg_to to;          /* optional variable binding      */
	void *udata;

	/* set only by the macro name */
	size_t min_count; /* ARG: 1 for ARG_ARG(S), 0 for ..._OPT */
};

/* Leaf-command marker for Level 1 dispatch without handlers (SDD 9.2). */
int arg_no_handler(arg *a);
#define ARG_NO_HANDLER arg_no_handler

/* ------------------------------------------------------------------ */
/* Declaration macros (SDD 6.2)                                       */
/*                                                                    */
/* Every leaf macro ends in `.help = __VA_ARGS__`, so optional fields */
/* simply continue the initialiser list:                              */
/*   ARG_INT('j', "jobs", "Parallel jobs", .def = "4", .min = "1")    */
/* ------------------------------------------------------------------ */

#define ARG__END { .kind = ARG_I_END }
#define ARG__FIRST(...) ARG__FIRST_(__VA_ARGS__, 0)
#define ARG__FIRST_(a, ...) a
#define ARG__REST(a, ...) __VA_ARGS__
/* children, then the terminator; works with zero children */
#define ARG__KIDS(...) ((const arg_item[]){ ARG__REST(__VA_ARGS__, ARG__END) })

#define ARG__OPT(k_, t_, s_, l_, ...)                                        \
	{ .kind = (k_), .type = (t_), .short_name = (s_), .name = (l_),      \
	  .help = __VA_ARGS__ }

#define ARG_FLAG(s, l, ...) ARG__OPT(ARG_I_FLAG, &arg_t_bool, s, l, __VA_ARGS__)
#define ARG_COUNT(s, l, ...) ARG__OPT(ARG_I_COUNT, NULL, s, l, __VA_ARGS__)
#define ARG_STR(s, l, ...)                                                   \
	ARG__OPT(ARG_I_VALUE, &arg_t_string, s, l, __VA_ARGS__)
#define ARG_INT(s, l, ...) ARG__OPT(ARG_I_VALUE, &arg_t_i64, s, l, __VA_ARGS__)
#define ARG_UINT(s, l, ...)                                                  \
	ARG__OPT(ARG_I_VALUE, &arg_t_u64, s, l, __VA_ARGS__)
#define ARG_DOUBLE(s, l, ...)                                                \
	ARG__OPT(ARG_I_VALUE, &arg_t_double, s, l, __VA_ARGS__)
#define ARG_SIZE(s, l, ...)                                                  \
	ARG__OPT(ARG_I_VALUE, &arg_t_bytesize, s, l, __VA_ARGS__)
#define ARG_DURATION(s, l, ...)                                              \
	ARG__OPT(ARG_I_VALUE, &arg_t_duration, s, l, __VA_ARGS__)
/* ARG_ENUM('f', "format", "Output format", ARG_CHOICES("a", "b"), ...) */
#define ARG_ENUM(s, l, h, ...)                                               \
	{ .kind = ARG_I_VALUE, .type = &arg_t_enum, .short_name = (s),       \
	  .name = (l), .help = (h), .names = __VA_ARGS__ }
#define ARG_CUSTOM(s, l, t, ...)                                             \
	ARG__OPT(ARG_I_VALUE, t, s, l, __VA_ARGS__)
#define ARG_DO(s, l, f, ...)                                                 \
	{ .kind = ARG_I_DO, .short_name = (s), .name = (l), .fn.on_do = (f), \
	  .help = __VA_ARGS__ }
#define ARG_ON(s, l, f, ...)                                                 \
	{ .kind = ARG_I_ON, .short_name = (s), .name = (l),                  \
	  .fn.on_each = (f), .help = __VA_ARGS__ }

/* Positionals; typed with .type = &arg_t_int etc. (default: string). */
#define ARG_ARG(n, ...)                                                      \
	{ .kind = ARG_I_ARG, .name = (n), .min_count = 1, .help = __VA_ARGS__ }
#define ARG_ARG_OPT(n, ...)                                                  \
	{ .kind = ARG_I_ARG, .name = (n), .min_count = 0, .help = __VA_ARGS__ }
#define ARG_ARGS(n, ...)                                                     \
	{ .kind = ARG_I_ARG, .name = (n), .min_count = 1, .many = 1,         \
	  .help = __VA_ARGS__ }
#define ARG_ARGS_OPT(n, ...)                                                 \
	{ .kind = ARG_I_ARG, .name = (n), .min_count = 0, .many = 1,         \
	  .help = __VA_ARGS__ }

/* ARG_CMD(name, fn or NULL, help, child items...) */
#define ARG_CMD(n, f, ...)                                                   \
	{ .kind = ARG_I_CMD, .name = (n), .fn.run = (f),                     \
	  .help = ARG__FIRST(__VA_ARGS__), .items = ARG__KIDS(__VA_ARGS__) }
/* Root command for Level 1 arrays: ARG_APP(name, summary, items...) */
#define ARG_APP(n, ...)                                                      \
	{ .kind = ARG_I_CMD, .name = (n), .help = ARG__FIRST(__VA_ARGS__),   \
	  .items = ARG__KIDS(__VA_ARGS__) }
#define ARG_MAIN(f) { .kind = ARG_I_MAIN, .fn.run = (f) }
#define ARG_ALIAS(n) { .kind = ARG_I_ALIAS, .name = (n) }
#define ARG_GROUP(title) { .kind = ARG_I_GROUP, .help = (title) }
#define ARG_EXAMPLE(cmdline, text)                                           \
	{ .kind = ARG_I_EXAMPLE, .name = (cmdline), .help = (text) }

/* Command rules (SDD 10.1); members by long name or "-x". */
#define ARG__RULE(k, ...)                                                    \
	{ .kind = ARG_I_RULE, .rule_kind = (k),                              \
	  .names = ARG_CHOICES(__VA_ARGS__) }
#define ARG_MUTEX(...) ARG__RULE(ARG_R_MUTEX, __VA_ARGS__)
#define ARG_ONE_OF(...) ARG__RULE(ARG_R_ONE_OF, __VA_ARGS__)
#define ARG_ANY_OF(...) ARG__RULE(ARG_R_ANY_OF, __VA_ARGS__)
#define ARG_REQUIRES(...) ARG__RULE(ARG_R_REQUIRES, __VA_ARGS__)
#define ARG_CONFLICTS(...) ARG__RULE(ARG_R_CONFLICTS, __VA_ARGS__)
#define ARG_RULE_FN(f, ud)                                                   \
	{ .kind = ARG_I_RULE, .rule_kind = ARG_R_CUSTOM, .fn.rule = (f),     \
	  .udata = (ud) }

/* Splitting a tree across files (SDD 6.6). */
#define ARG_DEFINE(tree, ...) const arg_item tree[] = { __VA_ARGS__, ARG__END }
#define ARG_EXTERN(tree) extern const arg_item tree[]
#define ARG_USE(tree) { .kind = ARG_I_USE, .items = (tree) }

/* ------------------------------------------------------------------ */
/* Configuration (SDD 6.7, 8.5)                                       */
/* ------------------------------------------------------------------ */

/* cfg.style flags */
#define ARG_STYLE_GNU 0x0000u             /* interleave options/args   */
#define ARG_STYLE_POSIX 0x0001u           /* stop at first positional  */
#define ARG_ABBREV 0x0002u                /* unique-prefix matching    */
#define ARG_RESPONSE_FILES 0x0004u        /* @file expansion           */
#define ARG_NO_AUTO_HELP 0x0008u          /* no automatic -h/--help    */
#define ARG_STRICT_VALUES 0x0010u         /* reject option-like values */
#define ARG_ALLOW_UNKNOWN 0x0020u         /* unknown -> arg_rest()     */
#define ARG_HONOR_POSIXLY_CORRECT 0x0040u /* POSIXLY_CORRECT env       */
#define ARG_HELP_COMMAND 0x0080u          /* "prog help <cmd>"         */

/* value sources, also used as bits in cfg.rule_sources */
typedef enum arg_source_kind {
	ARG_SRC_NONE = 0,
	ARG_SRC_DEFAULT = 1,
	ARG_SRC_ENV = 2,
	ARG_SRC_SOURCE = 3,
	ARG_SRC_CLI = 4
} arg_source_kind;

#define ARG_SRC_BIT(s) (1u << (s))
#define ARG_RULE_SOURCES_DEFAULT                                             \
	(ARG_SRC_BIT(ARG_SRC_CLI) | ARG_SRC_BIT(ARG_SRC_SOURCE) |             \
	 ARG_SRC_BIT(ARG_SRC_ENV))

#define ARG_CONFIG_VERSION 1u

struct arg_config {
	uint32_t version;         /* set by ARG_CONFIG_INIT            */
	uint32_t style;           /* ARG_STYLE_* | ARG_* flags          */
	const char *version_text; /* enables -V/--version              */
	void *udata;              /* returned by arg_udata()           */
	arg_write_fn write_out, write_err;
	void *io_ud;
	arg_getenv_fn getenv;
	void *env_ud;
	arg_alloc_fn alloc;
	void *alloc_ud;
	arg_source_fn sources[ARG_MAX_SOURCES];
	void *src_ud;
	arg_error_fn on_error;
	arg_help_fn render_help;
	arg_suggest_fn suggest;
	arg_unknown_fn on_unknown_cmd;
	const arg_messages *messages; /* NULL entries fall back       */
	void *arena;
	size_t arena_cap;
	unsigned help_width;     /* 0 = term_width hook, COLUMNS, 80 */
	arg_width_fn term_width;
	void *width_ud;
	unsigned rule_sources;   /* 0 = ARG_RULE_SOURCES_DEFAULT       */
	arg_exit_fn exit;        /* NULL = exit()                      */
	void *exit_ud;
};

#define ARG_CONFIG_INIT { .version = ARG_CONFIG_VERSION }

/* realloc()/free() based allocator for cfg.alloc (hosted builds only). */
void *arg_alloc_stdlib(void *ud, void *ptr, size_t old_size,
		       size_t new_size);

/* ------------------------------------------------------------------ */
/* Messages (SDD 8.5, 12.2)                                           */
/* ------------------------------------------------------------------ */

/* Message templates; the only substitution is %s, and %% for '%'. */
typedef enum arg_msg_id {
	ARG_MSG_UNKNOWN_OPTION = 0, /* "unknown option '%s'"              */
	ARG_MSG_AMBIGUOUS,          /* "option '%s' is ambiguous"         */
	ARG_MSG_MISSING_VALUE,      /* "option '%s' requires a value"     */
	ARG_MSG_UNEXPECTED_VALUE,   /* "option '%s' doesn't take a value" */
	ARG_MSG_BAD_VALUE,          /* "invalid value '%s' for '%s'"      */
	ARG_MSG_RANGE,              /* "value '%s' for '%s' is out of range" */
	ARG_MSG_REPEATED,           /* "option '%s' may only be given once" */
	ARG_MSG_MISSING_REQUIRED,   /* "missing required option '%s'"     */
	ARG_MSG_TOO_MANY_ARGS,      /* "unexpected argument '%s'"         */
	ARG_MSG_TOO_FEW_ARGS,       /* "missing required argument %s"     */
	ARG_MSG_UNKNOWN_COMMAND,    /* "unknown command '%s'"             */
	ARG_MSG_MISSING_COMMAND,    /* "missing command"                  */
	ARG_MSG_RULE,               /* "%s"                               */
	ARG_MSG_NO_MEMORY,          /* "out of memory"                    */
	ARG_MSG_IO,                 /* "cannot read '%s'"                 */
	ARG_MSG_SPEC,               /* "invalid specification: %s"        */
	ARG_MSG_ACTION,             /* "%s"                               */
	ARG_MSG_DID_YOU_MEAN,       /* "Did you mean '%s'?"               */
	ARG_MSG_TRY_HELP,           /* "Try '%s --help' for more information." */
	ARG_MSG_USAGE,              /* "Usage:"                           */
	ARG_MSG_ARGUMENTS,          /* "Arguments:"                       */
	ARG_MSG_OPTIONS,            /* "Options:"                         */
	ARG_MSG_COMMANDS,           /* "Commands:"                        */
	ARG_MSG_EXAMPLES,           /* "Examples:"                        */
	ARG_MSG_HELP_FLAG,          /* "Print help"                       */
	ARG_MSG_VERSION_FLAG,       /* "Print version"                    */
	ARG_MSG_DEFAULT,            /* "default"                          */
	ARG_MSG_ENV,                /* "env"                              */
	ARG_MSG_POSSIBLE,           /* "possible"                         */
	ARG_MSG_SEE_COMMAND_HELP,   /* "See '%s <command> --help' for more." */
	ARG_MSG_DEPRECATED,         /* "option '%s' is deprecated: %s"    */
	ARG_MSG_COUNT
} arg_msg_id;

struct arg_messages {
	const char *text[ARG_MSG_COUNT];
};

/* The built-in English table. */
extern const arg_messages arg_default_messages;

/* ------------------------------------------------------------------ */
/* Errors (SDD 12.2)                                                  */
/* ------------------------------------------------------------------ */

struct arg_error {
	arg_status status;
	int argi;              /* index in argv, -1 if not tied to one */
	size_t offset;         /* byte offset inside argv[argi]        */
	const arg_item *cmd;   /* command being parsed                 */
	const arg_item *item;  /* option involved, may be NULL         */
	const char *text;      /* offending input (borrowed)           */
	size_t text_len;
	const char *suggestion; /* bare name, e.g. "jobs"; may be NULL  */
	const char *detail;     /* from arg_fail()/arg_reject()          */
	const char *suggestion_prefix; /* "--", "-" or "" (commands)    */
};

/* ------------------------------------------------------------------ */
/* Level 2: pull tokenizer (SDD 6.8, 7.1)                             */
/* ------------------------------------------------------------------ */

/* Tokenizer-only style bit: "-<digit>..." is a positional. */
#define ARG_TOK_NEGATIVE_NUMBERS 0x10000u

typedef enum arg_tok_kind {
	ARG_T_SHORT = 1, /* one short option char of "-abc"           */
	ARG_T_LONG,      /* "--name" or "--name=value"                */
	ARG_T_POSITIONAL,/* anything else, including "-"              */
	ARG_T_END        /* "--" itself                               */
} arg_tok_kind;

typedef struct arg_tok {
	arg_tok_kind kind;
	int argi;          /* argv index the token came from          */
	size_t offset;     /* byte offset inside argv[argi]           */
	char ch;           /* SHORT: the option character             */
	const char *name;  /* LONG: name without "--", not terminated */
	size_t name_len;
	const char *value; /* LONG: after '=', or NULL; POSITIONAL: arg */
} arg_tok;

typedef struct arg_tokenizer {
	int argc;
	char *const *argv;
	uint32_t style;    /* ARG_STYLE_POSIX, ARG_TOK_NEGATIVE_NUMBERS */
	int argi;          /* next argv index                         */
	size_t pos;        /* > 0 while inside a short bundle         */
	bool after_dashdash;
	bool posix_stopped;
} arg_tokenizer;

/* argv[0] is skipped. argc < 0 and NULL entries are treated as end. */
void arg_tok_init(arg_tokenizer *tz, int argc, char *const *argv,
		  uint32_t style);
/* Next token; false at the end. */
bool arg_tok_next(arg_tokenizer *tz, arg_tok *t);
/*
 * Value for the last token: the rest of a short bundle ("-ofile"), or
 * else the next argument even if it starts with '-'. NULL at the end
 * of argv. Not valid after a LONG token that already has a value.
 */
const char *arg_tok_take_value(arg_tokenizer *tz);
/* Only an attached value: rest of the short bundle, else NULL. */
const char *arg_tok_take_attached(arg_tokenizer *tz);
/* POSIX mode: allow options again after a subcommand name. */
void arg_tok_resume_options(arg_tokenizer *tz);

/* ------------------------------------------------------------------ */
/* Level 1: parser (SDD 5.4, 6.7)                                     */
/* ------------------------------------------------------------------ */

#define ARG__PRIV_BYTES (768 + (ARG_MAX_DEPTH) * 2 * sizeof(void *))

/* Size is public so the parser can live on the stack; fields are not. */
struct arg_parser {
	union {
		unsigned char bytes[ARG__PRIV_BYTES];
		void *align_p;
		uint64_t align_u;
		double align_d;
	} _p;
	union {
		unsigned char bytes[ARG_INLINE_ARENA];
		void *align_p;
		uint64_t align_u;
		double align_d;
	} arena_inline;
};

/*
 * Prepare a parser for the root command items[0] (an ARG_APP / ARG_CMD
 * item). cfg may be NULL. No allocation. arg_init() captures this
 * translation unit's ARG_EXIT_ON_ERROR.
 */
void arg__init(arg *p, const arg_item *items, const arg_config *cfg,
	       int exit_on_error);
#define arg_init(p, items, cfg) arg__init((p), (items), (cfg), ARG_EXIT_ON_ERROR)

/* Parse argv; argv is never modified. */
arg_status arg_parse(arg *p, int argc, char *const *argv);
/* Print help, version or the error; return the exit code. */
int arg_report(arg *p);
/* Run the pending ARG_DO, or else the selected command's handler. */
int arg_run(arg *p);
/* Release memory obtained from cfg.alloc; idempotent. */
void arg_free(arg *p);
/* The current error (status ARG_OK when there is none). */
const arg_error *arg_error_get(const arg *p);
/*
 * Report an error from a handler, ARG_ON or check function. msg must
 * stay valid until it is printed. Returns 1, so `return arg_fail(...)`
 * works everywhere.
 */
int arg_fail(arg *a, const char *msg);
/*
 * Check a spec ahead of time (tests, CI). err may be NULL. Returns
 * ARG_OK or ARG_E_SPEC with err->detail describing the problem.
 */
arg_status arg_compile(const arg_item *items, const arg_config *cfg,
		       arg_error *err);
/* Unknown arguments collected with ARG_ALLOW_UNKNOWN. */
size_t arg_rest_count(const arg *a);
const char *arg_rest_at(const arg *a, size_t i);

/*
 * Walk a spec tree depth-first. fn returns non-zero to stop; that value
 * is returned. ARG_USE items are followed transparently.
 */
typedef int (*arg_visit_fn)(void *ud, const arg_item *item,
			    const arg_item *parent, unsigned depth);
int arg_foreach_item(const arg_item *root, arg_visit_fn fn, void *ud);

/* ------------------------------------------------------------------ */
/* Getters (SDD 6.4)                                                  */
/*                                                                    */
/* Keys: .id, else long name, else "-x", positionals and commands by  */
/* name. Lookup searches the selected command, then its ancestors.    */
/* ------------------------------------------------------------------ */

bool arg_flag(arg *a, const char *key);
int64_t arg_int(arg *a, const char *key);
uint64_t arg_uint(arg *a, const char *key);
double arg_double(arg *a, const char *key);
const char *arg_str(arg *a, const char *key);
int arg_enum(arg *a, const char *key); /* -1 when unset */
size_t arg_count(arg *a, const char *key);
const char *arg_str_at(arg *a, const char *key, size_t i);
int64_t arg_int_at(arg *a, const char *key, size_t i);
uint64_t arg_uint_at(arg *a, const char *key, size_t i);
double arg_double_at(arg *a, const char *key, size_t i);
/* Copy a value of any type into out (type->size bytes). */
bool arg_get(arg *a, const char *key, void *out);
bool arg_get_at(arg *a, const char *key, size_t i, void *out);
bool arg_is_set(arg *a, const char *key);
arg_source_kind arg_source(arg *a, const char *key);
/* Selected command path, e.g. "pkg repo add". */
const char *arg_path(arg *a);
/* true if the selected path below the root equals path ("repo add"). */
bool arg_path_is(arg *a, const char *path);
void *arg_udata(arg *a);

/* ------------------------------------------------------------------ */
/* Help and completion (SDD 11)                                       */
/* ------------------------------------------------------------------ */

/*
 * Render help for the command at path below the root ("repo add"),
 * NULL = the selected command. Returns 0 on success.
 */
int arg_help(arg *p, const char *path, arg_write_fn write, void *ud);

#define ARG_SH_BASH 0x1u
#define ARG_SH_ZSH 0x2u
#define ARG_SH_FISH 0x4u

/* Emit a completion script for one shell. Returns 0 on success. */
int arg_completion(arg *p, unsigned shell, arg_write_fn write, void *ud);

/* ------------------------------------------------------------------ */
/* Level 0 (SDD 6.1)                                                  */
/* ------------------------------------------------------------------ */

int arg__main(int exit_on_error, const arg_config *cfg, int argc,
	      char *const *argv, const arg_item *root);

/* return ARG_RUN(argc, argv, "name", "summary", items...); */
#define ARG_RUN(argc, argv, ...)                                             \
	arg__main(ARG_EXIT_ON_ERROR, NULL, (argc), (argv),                   \
		  (const arg_item[]){ ARG_APP(__VA_ARGS__) })
#define ARG_RUN_EX(cfg, argc, argv, ...)                                     \
	arg__main(ARG_EXIT_ON_ERROR, (cfg), (argc), (argv),                  \
		  (const arg_item[]){ ARG_APP(__VA_ARGS__) })

#ifdef __cplusplus
}
#endif

#endif /* ARG_H */
