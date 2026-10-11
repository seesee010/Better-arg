// SPDX-License-Identifier: GPL-2.0-only
/*
 * test_error.c - status codes, message table and error output
 *
 * The value items below use ARG_CUSTOM with local types instead of
 * ARG_INT / ARG_FLAG / ARG_STR: those reference the built-in types
 * from types.c, and these tests only need a type name (the metavar).
 */
#include "arg_internal.h"
#include "test.h"

/* ------------------------------------------------------------------ */
/* Fixtures                                                           */
/* ------------------------------------------------------------------ */

static const arg_type t_n = { .name = "N" };
static const arg_type t_anon = { .name = NULL };

static const arg_item jobs = ARG_CUSTOM('j', "jobs", &t_n, "Jobs");
static const arg_item jobs_short = ARG_CUSTOM('j', NULL, &t_n, "Jobs");
static const arg_item out = ARG_CUSTOM('o', "out", &t_n, "Output",
				       .value = "DIR");
static const arg_item color = ARG_CUSTOM(0, "color", &t_n, "Color",
					 .value = "WHEN", .optional_value = 1);
static const arg_item level = ARG_CUSTOM('l', "level", &t_n, "Level",
					 .optional_value = 1);
static const arg_item untyped = ARG_CUSTOM('x', "x-val", NULL, "X");
static const arg_item anon = ARG_CUSTOM('a', "anon", &t_anon, "Anon");
static const arg_item verbose = { .kind = ARG_I_FLAG, .short_name = 'v',
				  .name = "verbose", .help = "Verbose" };
static const arg_item vcount = ARG_COUNT('v', NULL, "Verbosity");
static const arg_item ver = ARG_DO('V', "version", NULL, "Version");
static const arg_item define = ARG_ON('D', "define", NULL, "Define",
				      .value = "K=V");
static const arg_item define_opt = ARG_ON('D', "define", NULL, "Define",
					  .value = "K=V",
					  .optional_value = 1);
static const arg_item quiet = ARG_ON('q', NULL, NULL, "Quiet");
static const arg_item nameless = { .kind = ARG_I_FLAG };
static const arg_item empty_long = { .kind = ARG_I_FLAG, .short_name = 'z',
				     .name = "" };
static const arg_item url = ARG_ARG("url", "Repository URL");
static const arg_item files = ARG_ARGS("files", "Files");
static const arg_item pos_anon = { .kind = ARG_I_ARG };
static const arg_item pkg = ARG_APP("pkg", "Package tool");
static const arg_item repo = ARG_CMD("repo", NULL, "Repositories");
static const arg_item add = ARG_CMD("add", NULL, "Add a repository");
static const arg_item group = ARG_GROUP("Network");
static const arg_item example = ARG_EXAMPLE("pkg add x", "Add x");
static const arg_item main_fn = ARG_MAIN(NULL);

static const struct arg__path mytool = { "mytool", NULL, 0 };

/* A write hook that fails on call number fail_at (0-based, -1 never). */
struct fhook {
	struct test_buf buf;
	int fail_at;
	int calls;
	int empty_writes;
};

static int fhook_write(void *ud, const char *s, size_t n)
{
	struct fhook *f = ud;

	if (n == 0)
		f->empty_writes++;
	if (f->calls++ == f->fail_at)
		return -7;
	return test_buf_write(&f->buf, s, n);
}

static void fhook_reset(struct fhook *f, int fail_at)
{
	test_buf_reset(&f->buf);
	f->fail_at = fail_at;
	f->calls = 0;
	f->empty_writes = 0;
}

typedef int (*op_fn)(arg_write_fn w, void *ud, const void *ctx);

/*
 * Run op once to count its writes, then once per write with that write
 * failing: every run must return exactly the hook's -7.
 */
static void check_failures(struct test_state *t_, op_fn op, const void *ctx)
{
	struct fhook f;
	int total, k;

	fhook_reset(&f, -1);
	CHECK_INT(op(fhook_write, &f, ctx), 0);
	CHECK_INT(f.empty_writes, 0);
	total = f.calls;
	CHECK(total > 0);
	for (k = 0; k < total; k++) {
		fhook_reset(&f, k);
		CHECK_INT(op(fhook_write, &f, ctx), -7);
		CHECK_INT(f.calls, k + 1);
	}
}

/* Render e for path into b; returns the hook result. */
static int render(struct test_buf *b, const arg_error *e,
		  const struct arg__path *path, const arg_messages *m)
{
	test_buf_reset(b);
	return arg__write_error(test_buf_write, b, e, path, m);
}

/* ------------------------------------------------------------------ */
/* Status codes                                                       */
/* ------------------------------------------------------------------ */

TEST(status_names)
{
	static const struct {
		arg_status st;
		const char *name;
	} cases[] = {
		{ ARG_OK, "ARG_OK" },
		{ ARG_EXIT_HELP, "ARG_EXIT_HELP" },
		{ ARG_EXIT_VERSION, "ARG_EXIT_VERSION" },
		{ ARG_E_UNKNOWN_OPTION, "ARG_E_UNKNOWN_OPTION" },
		{ ARG_E_AMBIGUOUS, "ARG_E_AMBIGUOUS" },
		{ ARG_E_MISSING_VALUE, "ARG_E_MISSING_VALUE" },
		{ ARG_E_UNEXPECTED_VALUE, "ARG_E_UNEXPECTED_VALUE" },
		{ ARG_E_BAD_VALUE, "ARG_E_BAD_VALUE" },
		{ ARG_E_RANGE, "ARG_E_RANGE" },
		{ ARG_E_REPEATED, "ARG_E_REPEATED" },
		{ ARG_E_MISSING_REQUIRED, "ARG_E_MISSING_REQUIRED" },
		{ ARG_E_TOO_MANY_ARGS, "ARG_E_TOO_MANY_ARGS" },
		{ ARG_E_TOO_FEW_ARGS, "ARG_E_TOO_FEW_ARGS" },
		{ ARG_E_UNKNOWN_COMMAND, "ARG_E_UNKNOWN_COMMAND" },
		{ ARG_E_MISSING_COMMAND, "ARG_E_MISSING_COMMAND" },
		{ ARG_E_RULE, "ARG_E_RULE" },
		{ ARG_E_NO_MEMORY, "ARG_E_NO_MEMORY" },
		{ ARG_E_IO, "ARG_E_IO" },
		{ ARG_E_SPEC, "ARG_E_SPEC" },
		{ ARG_E_ACTION, "ARG_E_ACTION" },
	};
	size_t i;

	CHECK_UINT(ARG__COUNTOF(cases), ARG_STATUS_COUNT);
	for (i = 0; i < ARG__COUNTOF(cases); i++)
		CHECK_STR(arg_status_name(cases[i].st), cases[i].name);
}

TEST(status_name_out_of_range)
{
	CHECK_STR(arg_status_name(ARG_STATUS_COUNT), "ARG_E_UNKNOWN_STATUS");
	CHECK_STR(arg_status_name((arg_status)100), "ARG_E_UNKNOWN_STATUS");
	CHECK_STR(arg_status_name((arg_status)-1), "ARG_E_UNKNOWN_STATUS");
}

TEST(exit_codes)
{
	static const struct {
		arg_status st;
		int code;
	} cases[] = {
		{ ARG_OK, 0 },
		{ ARG_EXIT_HELP, 0 },
		{ ARG_EXIT_VERSION, 0 },
		{ ARG_E_UNKNOWN_OPTION, 2 },
		{ ARG_E_AMBIGUOUS, 2 },
		{ ARG_E_MISSING_VALUE, 2 },
		{ ARG_E_UNEXPECTED_VALUE, 2 },
		{ ARG_E_BAD_VALUE, 2 },
		{ ARG_E_RANGE, 2 },
		{ ARG_E_REPEATED, 2 },
		{ ARG_E_MISSING_REQUIRED, 2 },
		{ ARG_E_TOO_MANY_ARGS, 2 },
		{ ARG_E_TOO_FEW_ARGS, 2 },
		{ ARG_E_UNKNOWN_COMMAND, 2 },
		{ ARG_E_MISSING_COMMAND, 2 },
		{ ARG_E_RULE, 2 },
		{ ARG_E_NO_MEMORY, 70 },
		{ ARG_E_IO, 66 },
		{ ARG_E_SPEC, 70 },
		{ ARG_E_ACTION, 1 },
	};
	size_t i;

	CHECK_UINT(ARG__COUNTOF(cases), ARG_STATUS_COUNT);
	for (i = 0; i < ARG__COUNTOF(cases); i++)
		CHECK_INT(arg_exit_code(cases[i].st), cases[i].code);
	CHECK_INT(arg_exit_code(ARG_STATUS_COUNT), 70);
	CHECK_INT(arg_exit_code((arg_status)-1), 70);
	CHECK_INT(arg_exit_code((arg_status)1000), 70);
}

/* ------------------------------------------------------------------ */
/* Message table                                                      */
/* ------------------------------------------------------------------ */

TEST(default_messages_complete)
{
	size_t i;

	for (i = 0; i < ARG_MSG_COUNT; i++) {
		CHECK(arg_default_messages.text[i] != NULL);
		if (arg_default_messages.text[i])
			CHECK(arg_default_messages.text[i][0] != '\0');
	}
}

TEST(default_messages_text)
{
	static const struct {
		arg_msg_id id;
		const char *text;
	} cases[] = {
		{ ARG_MSG_UNKNOWN_OPTION, "unknown option '%s'" },
		{ ARG_MSG_AMBIGUOUS, "option '%s' is ambiguous" },
		{ ARG_MSG_MISSING_VALUE, "option '%s' requires a value" },
		{ ARG_MSG_UNEXPECTED_VALUE,
		  "option '%s' doesn't take a value" },
		{ ARG_MSG_BAD_VALUE, "invalid value '%s' for '%s'" },
		{ ARG_MSG_RANGE, "value '%s' for '%s' is out of range" },
		{ ARG_MSG_REPEATED, "option '%s' may only be given once" },
		{ ARG_MSG_MISSING_REQUIRED, "missing required option '%s'" },
		{ ARG_MSG_TOO_MANY_ARGS, "unexpected argument '%s'" },
		{ ARG_MSG_TOO_FEW_ARGS, "missing required argument %s" },
		{ ARG_MSG_UNKNOWN_COMMAND, "unknown command '%s'" },
		{ ARG_MSG_MISSING_COMMAND, "missing command" },
		{ ARG_MSG_RULE, "%s" },
		{ ARG_MSG_NO_MEMORY, "out of memory" },
		{ ARG_MSG_IO, "cannot read '%s'" },
		{ ARG_MSG_SPEC, "invalid specification: %s" },
		{ ARG_MSG_ACTION, "%s" },
		{ ARG_MSG_DID_YOU_MEAN, "Did you mean '%s'?" },
		{ ARG_MSG_TRY_HELP, "Try '%s --help' for more information." },
		{ ARG_MSG_USAGE, "Usage:" },
		{ ARG_MSG_ARGUMENTS, "Arguments:" },
		{ ARG_MSG_OPTIONS, "Options:" },
		{ ARG_MSG_COMMANDS, "Commands:" },
		{ ARG_MSG_EXAMPLES, "Examples:" },
		{ ARG_MSG_HELP_FLAG, "Print help" },
		{ ARG_MSG_VERSION_FLAG, "Print version" },
		{ ARG_MSG_DEFAULT, "default" },
		{ ARG_MSG_ENV, "env" },
		{ ARG_MSG_POSSIBLE, "possible" },
		{ ARG_MSG_SEE_COMMAND_HELP,
		  "See '%s <command> --help' for more." },
		{ ARG_MSG_DEPRECATED, "option '%s' is deprecated: %s" },
	};
	size_t i;

	CHECK_UINT(ARG__COUNTOF(cases), ARG_MSG_COUNT);
	for (i = 0; i < ARG__COUNTOF(cases); i++) {
		CHECK_STR(arg_default_messages.text[cases[i].id],
			  cases[i].text);
		CHECK_STR(arg__msg(NULL, cases[i].id), cases[i].text);
	}
}

TEST(msg_custom_table_and_fallback)
{
	arg_messages m;

	memset(&m, 0, sizeof(m));
	m.text[ARG_MSG_USAGE] = "Aufruf:";
	CHECK_STR(arg__msg(&m, ARG_MSG_USAGE), "Aufruf:");
	CHECK_STR(arg__msg(&m, ARG_MSG_OPTIONS), "Options:");
	CHECK_STR(arg__msg(&arg_default_messages, ARG_MSG_ENV), "env");
	CHECK_STR(arg__msg(NULL, ARG_MSG_COUNT), "");
	CHECK_STR(arg__msg(&m, ARG_MSG_COUNT), "");
	CHECK_STR(arg__msg(NULL, (arg_msg_id)-1), "");
	CHECK_STR(arg__msg(NULL, (arg_msg_id)500), "");
}

/* ------------------------------------------------------------------ */
/* arg__format                                                        */
/* ------------------------------------------------------------------ */

static int fmt_into(struct test_buf *b, const char *fmt,
		    const struct arg__str *args, size_t nargs)
{
	test_buf_reset(b);
	return arg__format(test_buf_write, b, fmt, args, nargs);
}

TEST(format_basic)
{
	static const struct arg__str x[] = { { "X", 1, false },
					     { "YZ", 2, false } };
	struct test_buf b;

	CHECK_INT(fmt_into(&b, "plain", NULL, 0), 0);
	CHECK_STR(b.data, "plain");
	CHECK_INT(fmt_into(&b, "a %s b", x, 1), 0);
	CHECK_STR(b.data, "a X b");
	CHECK_INT(fmt_into(&b, "%s%s", x, 2), 0);
	CHECK_STR(b.data, "XYZ");
	CHECK_INT(fmt_into(&b, "<%s|%s>", x, 2), 0);
	CHECK_STR(b.data, "<X|YZ>");
	CHECK_INT(fmt_into(&b, "", x, 2), 0);
	CHECK_STR(b.data, "");
	CHECK_INT(fmt_into(&b, NULL, x, 2), 0);
	CHECK_STR(b.data, "");
}

TEST(format_percent)
{
	static const struct arg__str x[] = { { "X", 1, false } };
	struct test_buf b;

	CHECK_INT(fmt_into(&b, "100%%", NULL, 0), 0);
	CHECK_STR(b.data, "100%");
	CHECK_INT(fmt_into(&b, "%%s", x, 1), 0);
	CHECK_STR(b.data, "%s");
	CHECK_INT(fmt_into(&b, "%%%s", x, 1), 0);
	CHECK_STR(b.data, "%X");
	CHECK_INT(fmt_into(&b, "%%%%", NULL, 0), 0);
	CHECK_STR(b.data, "%%");
	CHECK_INT(fmt_into(&b, "a%%b%%c", NULL, 0), 0);
	CHECK_STR(b.data, "a%b%c");
}

TEST(format_unknown_directives_are_literal)
{
	static const struct arg__str x[] = { { "X", 1, false } };
	struct test_buf b;

	CHECK_INT(fmt_into(&b, "%d %x %n %p %-5s", x, 1), 0);
	CHECK_STR(b.data, "%d %x %n %p %-5s");
	CHECK_INT(fmt_into(&b, "end %", x, 1), 0);
	CHECK_STR(b.data, "end %");
	CHECK_INT(fmt_into(&b, "%", NULL, 0), 0);
	CHECK_STR(b.data, "%");
	CHECK_INT(fmt_into(&b, "%S%s", x, 1), 0);
	CHECK_STR(b.data, "%SX");
}

TEST(format_missing_and_null_args)
{
	static const struct arg__str x[] = { { "X", 1, false },
					     { NULL, 5, false },
					     { "", 0, true } };
	struct test_buf b;

	CHECK_INT(fmt_into(&b, "a %s b %s", x, 1), 0);
	CHECK_STR(b.data, "a X b ");
	CHECK_INT(fmt_into(&b, "[%s]", NULL, 0), 0);
	CHECK_STR(b.data, "[]");
	CHECK_INT(fmt_into(&b, "[%s]", NULL, 3), 0);
	CHECK_STR(b.data, "[]");
	CHECK_INT(fmt_into(&b, "[%s][%s][%s][%s]", x, 3), 0);
	CHECK_STR(b.data, "[X][][][]");
}

TEST(format_length_is_honoured)
{
	static const struct arg__str x[] = { { "abcdef", 3, false },
					     { "ghijkl", 2, true } };
	struct test_buf b;

	CHECK_INT(fmt_into(&b, "%s-%s", x, 2), 0);
	CHECK_STR(b.data, "abc-gh");
}

TEST(format_untrusted_is_escaped)
{
	static const struct arg__str x[] = {
		{ "\x1b[2J", 4, true },
		{ "\x1b[2J", 4, false },
		{ "%s%n", 4, true },
	};
	struct test_buf b;

	CHECK_INT(fmt_into(&b, "'%s'", x, 1), 0);
	CHECK_STR(b.data, "'\\x1B[2J'");
	CHECK_INT(fmt_into(&b, "'%s'", x + 1, 1), 0);
	CHECK_STR(b.data, "'\x1b[2J'");
	/* user text is never interpreted as a template */
	CHECK_INT(fmt_into(&b, "'%s'", x + 2, 1), 0);
	CHECK_STR(b.data, "'%s%n'");
}

static int count_calls(const char *fmt, const struct arg__str *args,
		       size_t nargs)
{
	struct fhook f;

	fhook_reset(&f, -1);
	if (arg__format(fhook_write, &f, fmt, args, nargs))
		return -1;
	return f.empty_writes ? -2 : f.calls;
}

TEST(format_writes_runs)
{
	static const struct arg__str x[] = { { "X", 1, false },
					     { "", 0, false } };

	CHECK_INT(count_calls("a long literal %d text", NULL, 0), 1);
	CHECK_INT(count_calls("a %s b", x, 1), 3);
	CHECK_INT(count_calls("%s", x, 1), 1);
	CHECK_INT(count_calls("%s", x + 1, 1), 0);
	CHECK_INT(count_calls("", NULL, 0), 0);
	CHECK_INT(count_calls("ab%%cd", NULL, 0), 2);
}

struct fmt_ctx {
	const char *fmt;
	const struct arg__str *args;
	size_t nargs;
};

static int op_format(arg_write_fn w, void *ud, const void *ctx)
{
	const struct fmt_ctx *c = ctx;

	return arg__format(w, ud, c->fmt, c->args, c->nargs);
}

TEST(format_propagates_write_errors)
{
	static const struct arg__str x[] = { { "X", 1, false },
					     { "a\x01" "b", 3, true } };
	static const struct fmt_ctx c1 = { "a %s b %% c %s d", x, 2 };
	static const struct fmt_ctx c2 = { "%s%%", x, 2 };

	check_failures(t_, op_format, &c1);
	check_failures(t_, op_format, &c2);
}

/* ------------------------------------------------------------------ */
/* arg__write_escaped                                                 */
/* ------------------------------------------------------------------ */

#define B(s) s, sizeof(s) - 1

TEST(escape_table)
{
	static const struct {
		const char *in;
		size_t n;
		const char *want;
	} cases[] = {
		{ B(""), "" },
		{ B("abc"), "abc" },
		{ B(" ~"), " ~" },
		/* C0 controls and DEL */
		{ B("a\x01" "b"), "a\\x01b" },
		{ B("\x1f"), "\\x1F" },
		{ B("\x7f"), "\\x7F" },
		{ B("\n\t\r"), "\\x0A\\x09\\x0D" },
		{ B("\x1b[31mred"), "\\x1B[31mred" },
		{ B("a\0" "b"), "a\\x00b" },
		/* valid UTF-8 passes */
		{ B("\xc2\x80"), "\xc2\x80" },
		{ B("caf\xc3\xa9"), "caf\xc3\xa9" },
		{ B("\xdf\xbf"), "\xdf\xbf" },
		{ B("\xe0\xa0\x80"), "\xe0\xa0\x80" },
		{ B("\xe2\x82\xac"), "\xe2\x82\xac" },
		{ B("\xed\x9f\xbf"), "\xed\x9f\xbf" },
		{ B("\xee\x80\x80"), "\xee\x80\x80" },
		{ B("\xef\xbf\xbf"), "\xef\xbf\xbf" },
		{ B("\xf0\x90\x80\x80"), "\xf0\x90\x80\x80" },
		{ B("\xf0\x9f\x98\x80!"), "\xf0\x9f\x98\x80!" },
		{ B("\xf1\x80\x80\x80"), "\xf1\x80\x80\x80" },
		{ B("\xf3\xbf\xbf\xbf"), "\xf3\xbf\xbf\xbf" },
		{ B("\xf4\x8f\xbf\xbf"), "\xf4\x8f\xbf\xbf" },
		/* overlong forms */
		{ B("\xc0\x80"), "\\xC0\\x80" },
		{ B("\xc1\xbf"), "\\xC1\\xBF" },
		{ B("\xe0\x80\x80"), "\\xE0\\x80\\x80" },
		{ B("\xe0\x9f\xbf"), "\\xE0\\x9F\\xBF" },
		{ B("\xf0\x80\x80\x80"), "\\xF0\\x80\\x80\\x80" },
		{ B("\xf0\x8f\xbf\xbf"), "\\xF0\\x8F\\xBF\\xBF" },
		/* surrogates */
		{ B("\xed\xa0\x80"), "\\xED\\xA0\\x80" },
		{ B("\xed\xbf\xbf"), "\\xED\\xBF\\xBF" },
		/* above U+10FFFF and invalid lead bytes */
		{ B("\xf4\x90\x80\x80"), "\\xF4\\x90\\x80\\x80" },
		{ B("\xf5\x80\x80\x80"), "\\xF5\\x80\\x80\\x80" },
		{ B("\xfe\xff"), "\\xFE\\xFF" },
		/* lone continuation bytes */
		{ B("\x80"), "\\x80" },
		{ B("a\xbf" "b"), "a\\xBFb" },
		/* truncated sequences */
		{ B("\xc3"), "\\xC3" },
		{ B("\xe2\x82"), "\\xE2\\x82" },
		{ B("\xf0\x9f\x98"), "\\xF0\\x9F\\x98" },
		/* bad continuation bytes */
		{ B("\xc3("), "\\xC3(" },
		{ B("\xe2(\xa1"), "\\xE2(\\xA1" },
		{ B("\xe2\x82("), "\\xE2\\x82(" },
		{ B("\xf0\x9f(\x80"), "\\xF0\\x9F(\\x80" },
		{ B("\xf0\x9f\x98("), "\\xF0\\x9F\\x98(" },
		{ B("\xc3\xc3\xa9"), "\\xC3\xc3\xa9" },
	};
	struct test_buf b;
	size_t i;

	for (i = 0; i < ARG__COUNTOF(cases); i++) {
		test_buf_reset(&b);
		CHECK_INT(arg__write_escaped(test_buf_write, &b, cases[i].in,
					     cases[i].n), 0);
		if (strcmp(b.data, cases[i].want))
			fprintf(stderr, "  escape case %zu\n", i);
		CHECK_STR(b.data, cases[i].want);
	}
}

TEST(escape_null_and_partial)
{
	struct test_buf b;

	test_buf_reset(&b);
	CHECK_INT(arg__write_escaped(test_buf_write, &b, NULL, 5), 0);
	CHECK_STR(b.data, "");
	/* n stops in the middle of a valid sequence: truncated */
	CHECK_INT(arg__write_escaped(test_buf_write, &b, "\xe2\x82\xac", 2),
		  0);
	CHECK_STR(b.data, "\\xE2\\x82");
}

static int escape_calls(const char *s, size_t n)
{
	struct fhook f;

	fhook_reset(&f, -1);
	if (arg__write_escaped(fhook_write, &f, s, n))
		return -1;
	return f.empty_writes ? -2 : f.calls;
}

TEST(escape_writes_runs)
{
	CHECK_INT(escape_calls(B("plain text \xc3\xa9")), 1);
	CHECK_INT(escape_calls(B("ab\x01" "cd")), 3);
	CHECK_INT(escape_calls(B("\x01\x02")), 2);
	CHECK_INT(escape_calls(B("")), 0);
}

struct esc_ctx {
	const char *s;
	size_t n;
};

static int op_escape(arg_write_fn w, void *ud, const void *ctx)
{
	const struct esc_ctx *c = ctx;

	return arg__write_escaped(w, ud, c->s, c->n);
}

TEST(escape_propagates_write_errors)
{
	static const struct esc_ctx c = { B("ab\x01" "cd\x7f" "e") };

	check_failures(t_, op_escape, &c);
}

/* ------------------------------------------------------------------ */
/* arg__write_path                                                    */
/* ------------------------------------------------------------------ */

static const arg_item *const cmds3[] = { &pkg, &repo, &add };
static const arg_item *const cmds_hole[] = { &pkg, NULL, &main_fn };
static const arg_item *const cmds_null_root[] = { NULL };

static const char *path_str(struct test_buf *b, const struct arg__path *p)
{
	test_buf_reset(b);
	if (arg__write_path(test_buf_write, b, p))
		return "(error)";
	return b->data;
}

TEST(path_writing)
{
	const struct arg__path none = { NULL, NULL, 0 };
	const struct arg__path prog = { "mytool", NULL, 0 };
	const struct arg__path prog_depth = { "mytool", NULL, 5 };
	const struct arg__path empty_cmds = { NULL, cmds3, 0 };
	const struct arg__path d1 = { NULL, cmds3, 1 };
	const struct arg__path d3 = { NULL, cmds3, 3 };
	const struct arg__path d3_prog = { "pkg2", cmds3, 3 };
	const struct arg__path d2_prog = { "pkg2", cmds3, 2 };
	const struct arg__path hole = { NULL, cmds_hole, 3 };
	const struct arg__path null_root = { NULL, cmds_null_root, 1 };
	const struct arg__path evil = { "my\x1btool", NULL, 0 };
	struct test_buf b;

	CHECK_STR(path_str(&b, NULL), "?");
	CHECK_STR(path_str(&b, &none), "?");
	CHECK_STR(path_str(&b, &prog), "mytool");
	CHECK_STR(path_str(&b, &prog_depth), "mytool");
	CHECK_STR(path_str(&b, &empty_cmds), "?");
	CHECK_STR(path_str(&b, &d1), "pkg");
	CHECK_STR(path_str(&b, &d3), "pkg repo add");
	CHECK_STR(path_str(&b, &d3_prog), "pkg2 repo add");
	CHECK_STR(path_str(&b, &d2_prog), "pkg2 repo");
	CHECK_STR(path_str(&b, &hole), "pkg ? ?");
	CHECK_STR(path_str(&b, &null_root), "?");
	CHECK_STR(path_str(&b, &evil), "my\\x1Btool");
}

static int op_path(arg_write_fn w, void *ud, const void *ctx)
{
	return arg__write_path(w, ud, ctx);
}

TEST(path_propagates_write_errors)
{
	static const struct arg__path d3 = { "mytool", cmds3, 3 };
	static const struct arg__path none = { NULL, NULL, 0 };

	check_failures(t_, op_path, &d3);
	check_failures(t_, op_path, &none);
}

/* ------------------------------------------------------------------ */
/* arg__write_item_label                                              */
/* ------------------------------------------------------------------ */

TEST(item_labels)
{
	static const struct {
		const arg_item *it;
		bool metavar;
		const char *want;
	} cases[] = {
		{ &jobs, false, "--jobs" },
		{ &jobs, true, "--jobs <N>" },
		{ &jobs_short, false, "-j" },
		{ &jobs_short, true, "-j <N>" },
		{ &out, true, "--out <DIR>" },
		{ &color, false, "--color" },
		{ &color, true, "--color[=<WHEN>]" },
		{ &level, true, "--level[=<N>]" },
		{ &untyped, true, "--x-val <VALUE>" },
		{ &anon, true, "--anon <VALUE>" },
		{ &verbose, false, "--verbose" },
		{ &verbose, true, "--verbose" },
		{ &vcount, true, "-v" },
		{ &ver, true, "--version" },
		{ &define, false, "--define" },
		{ &define, true, "--define <K=V>" },
		{ &define_opt, true, "--define[=<K=V>]" },
		{ &quiet, true, "-q" },
		{ &nameless, true, "?" },
		{ &empty_long, false, "-z" },
		{ &url, false, "<url>" },
		{ &url, true, "<url>" },
		{ &files, true, "<files>..." },
		{ &pos_anon, false, "<?>" },
		{ &pkg, true, "pkg" },
		{ &add, false, "add" },
		{ &group, false, "?" },
		{ &example, false, "pkg add x" },
		{ &main_fn, true, "?" },
		{ NULL, true, "?" },
	};
	struct test_buf b;
	size_t i;

	for (i = 0; i < ARG__COUNTOF(cases); i++) {
		test_buf_reset(&b);
		CHECK_INT(arg__write_item_label(test_buf_write, &b,
						cases[i].it, cases[i].metavar),
			  0);
		CHECK_STR(b.data, cases[i].want);
	}
}

struct label_ctx {
	const arg_item *it;
	bool metavar;
};

static int op_label(arg_write_fn w, void *ud, const void *ctx)
{
	const struct label_ctx *c = ctx;

	return arg__write_item_label(w, ud, c->it, c->metavar);
}

TEST(label_propagates_write_errors)
{
	static const struct label_ctx cases[] = {
		{ &jobs, true }, { &color, true }, { &jobs_short, true },
		{ &files, false }, { &nameless, false }, { &add, false },
	};
	size_t i;

	for (i = 0; i < ARG__COUNTOF(cases); i++)
		check_failures(t_, op_label, &cases[i]);
}

/* ------------------------------------------------------------------ */
/* arg__write_error: golden output for every status                   */
/* ------------------------------------------------------------------ */

#define TRY_MYTOOL "Try 'mytool --help' for more information.\n"

static arg_error mk(arg_status st, const arg_item *item, const char *text,
		    const char *detail)
{
	arg_error e;

	memset(&e, 0, sizeof(e));
	e.status = st;
	e.argi = -1;
	e.item = item;
	e.text = text;
	e.detail = detail;
	return e;
}

TEST(error_unknown_option_with_suggestion)
{
	arg_error e = mk(ARG_E_UNKNOWN_OPTION, NULL, "--job", NULL);
	struct test_buf b;

	e.suggestion = "jobs";
	e.suggestion_prefix = "--";
	CHECK_INT(render(&b, &e, &mytool, NULL), 0);
	CHECK_STR(b.data, "mytool: unknown option '--job'\n"
			  "  Did you mean '--jobs'?\n" TRY_MYTOOL);
}

TEST(error_golden_all_statuses)
{
	static const struct arg__path remote = { "mytool", cmds3, 2 };
	static const struct {
		arg_error e;
		const struct arg__path *path;
		const char *want;
	} cases[] = {
		{ { ARG_E_UNKNOWN_OPTION, 1, 0, NULL, NULL, "-x", 0, NULL,
		    NULL, NULL },
		  &mytool, "mytool: unknown option '-x'\n" TRY_MYTOOL },
		{ { ARG_E_AMBIGUOUS, 1, 0, NULL, NULL, "--ver", 0, NULL,
		    NULL, NULL },
		  &mytool, "mytool: option '--ver' is ambiguous\n" TRY_MYTOOL },
		{ { ARG_E_AMBIGUOUS, 1, 0, NULL, NULL, "--ver", 0, NULL,
		    "could be --verbose, --version", NULL },
		  &mytool,
		  "mytool: option '--ver' is ambiguous: could be --verbose, "
		  "--version\n" TRY_MYTOOL },
		{ { ARG_E_MISSING_VALUE, 1, 0, NULL, &jobs, "-j", 0, NULL,
		    NULL, NULL },
		  &mytool, "mytool: option '--jobs' requires a value\n"
			   TRY_MYTOOL },
		{ { ARG_E_UNEXPECTED_VALUE, 1, 0, NULL, &verbose,
		    "--verbose=yes", 0, NULL, NULL, NULL },
		  &mytool, "mytool: option '--verbose' doesn't take a value\n"
			   TRY_MYTOOL },
		{ { ARG_E_BAD_VALUE, 2, 0, NULL, &jobs, "abc", 0, NULL,
		    "expected an integer", NULL },
		  &mytool, "mytool: invalid value 'abc' for '--jobs <N>': "
			   "expected an integer\n" TRY_MYTOOL },
		{ { ARG_E_BAD_VALUE, 2, 0, NULL, &color, "sometimes", 0, NULL,
		    NULL, NULL },
		  &mytool, "mytool: invalid value 'sometimes' for "
			   "'--color[=<WHEN>]'\n" TRY_MYTOOL },
		{ { ARG_E_RANGE, 2, 0, NULL, &jobs, "99", 0, NULL,
		    "must be at most 8", NULL },
		  &mytool, "mytool: value '99' for '--jobs <N>' is out of "
			   "range: must be at most 8\n" TRY_MYTOOL },
		{ { ARG_E_REPEATED, 3, 0, NULL, &out, "--out", 0, NULL, NULL,
		    NULL },
		  &mytool, "mytool: option '--out' may only be given once\n"
			   TRY_MYTOOL },
		{ { ARG_E_MISSING_REQUIRED, -1, 0, NULL, &out, NULL, 0, NULL,
		    NULL, NULL },
		  &mytool, "mytool: missing required option '--out'\n"
			   TRY_MYTOOL },
		{ { ARG_E_TOO_MANY_ARGS, 4, 0, NULL, NULL, "extra", 0, NULL,
		    NULL, NULL },
		  &mytool, "mytool: unexpected argument 'extra'\n" TRY_MYTOOL },
		{ { ARG_E_TOO_FEW_ARGS, -1, 0, NULL, &url, NULL, 0, NULL, NULL,
		    NULL },
		  &mytool, "mytool: missing required argument <url>\n"
			   TRY_MYTOOL },
		{ { ARG_E_TOO_FEW_ARGS, -1, 0, NULL, &files, NULL, 0, NULL,
		    NULL, NULL },
		  &mytool, "mytool: missing required argument <files>...\n"
			   TRY_MYTOOL },
		{ { ARG_E_UNKNOWN_COMMAND, 2, 0, &repo, NULL, "ad", 0, "add",
		    NULL, NULL },
		  &remote, "mytool repo: unknown command 'ad'\n"
			   "  Did you mean 'add'?\n"
			   "Try 'mytool repo --help' for more information.\n" },
		{ { ARG_E_MISSING_COMMAND, -1, 0, &repo, NULL, NULL, 0, NULL,
		    NULL, NULL },
		  &remote, "mytool repo: missing command\n"
			   "Try 'mytool repo --help' for more information.\n" },
		{ { ARG_E_RULE, -1, 0, NULL, NULL, NULL, 0, NULL,
		    "'--json' and '--yaml' cannot be used together", NULL },
		  &mytool, "mytool: '--json' and '--yaml' cannot be used "
			   "together\n" TRY_MYTOOL },
		{ { ARG_E_RULE, -1, 0, NULL, NULL, NULL, 0, NULL, NULL, NULL },
		  &mytool, "mytool: rule violated\n" TRY_MYTOOL },
		{ { ARG_E_NO_MEMORY, -1, 0, NULL, NULL, NULL, 0, NULL, NULL,
		    NULL },
		  &mytool, "mytool: out of memory\n" },
		{ { ARG_E_NO_MEMORY, -1, 0, NULL, NULL, NULL, 0, NULL,
		    "arena exhausted", NULL },
		  &mytool, "mytool: out of memory: arena exhausted\n" },
		{ { ARG_E_IO, 1, 0, NULL, NULL, "args.txt", 0, NULL,
		    "No such file or directory", NULL },
		  &mytool, "mytool: cannot read 'args.txt': No such file or "
			   "directory\n" },
		{ { ARG_E_SPEC, -1, 0, NULL, NULL, NULL, 0, NULL,
		    "duplicate key 'jobs'", NULL },
		  &mytool, "mytool: invalid specification: duplicate key "
			   "'jobs'\n" },
		{ { ARG_E_SPEC, -1, 0, NULL, NULL, NULL, 0, NULL, NULL, NULL },
		  &mytool, "mytool: invalid specification: unknown error\n" },
		{ { ARG_E_ACTION, -1, 0, NULL, NULL, NULL, 0, NULL,
		    "cannot connect", NULL },
		  &mytool, "mytool: cannot connect\n" },
		{ { ARG_E_ACTION, -1, 0, NULL, NULL, NULL, 0, NULL, NULL,
		    NULL },
		  &mytool, "mytool: action failed\n" },
		{ { (arg_status)99, -1, 0, NULL, NULL, NULL, 0, NULL, NULL,
		    NULL },
		  &mytool, "mytool: ARG_E_UNKNOWN_STATUS\n" },
		{ { (arg_status)99, -1, 0, NULL, NULL, NULL, 0, NULL, "x",
		    NULL },
		  &mytool, "mytool: ARG_E_UNKNOWN_STATUS: x\n" },
	};
	struct test_buf b;
	size_t i;

	for (i = 0; i < ARG__COUNTOF(cases); i++) {
		CHECK_INT(render(&b, &cases[i].e, cases[i].path, NULL), 0);
		if (strcmp(b.data, cases[i].want))
			fprintf(stderr, "  error case %zu (%s)\n", i,
				arg_status_name(cases[i].e.status));
		CHECK_STR(b.data, cases[i].want);
	}
}

TEST(error_success_statuses_print_nothing)
{
	static const arg_status st[] = { ARG_OK, ARG_EXIT_HELP,
					 ARG_EXIT_VERSION };
	struct test_buf b;
	size_t i;

	for (i = 0; i < ARG__COUNTOF(st); i++) {
		arg_error e = mk(st[i], &jobs, "x", "detail");

		e.suggestion = "jobs";
		CHECK_INT(render(&b, &e, &mytool, NULL), 0);
		CHECK_STR(b.data, "");
	}
	CHECK_INT(render(&b, NULL, &mytool, NULL), 0);
	CHECK_STR(b.data, "");
}

TEST(error_escapes_user_text)
{
	arg_error e = mk(ARG_E_UNKNOWN_OPTION, NULL, "--a\x1b]0;pwned\x07",
			 NULL);
	struct test_buf b;

	CHECK_INT(render(&b, &e, &mytool, NULL), 0);
	CHECK_STR(b.data, "mytool: unknown option '--a\\x1B]0;pwned\\x07'\n"
			  TRY_MYTOOL);

	e = mk(ARG_E_BAD_VALUE, &jobs, "1\xff", "expected an integer");
	CHECK_INT(render(&b, &e, &mytool, NULL), 0);
	CHECK_STR(b.data, "mytool: invalid value '1\\xFF' for '--jobs <N>': "
			  "expected an integer\n" TRY_MYTOOL);

	/* format directives in user text stay literal */
	e = mk(ARG_E_TOO_MANY_ARGS, NULL, "%s%n%%", NULL);
	CHECK_INT(render(&b, &e, &mytool, NULL), 0);
	CHECK_STR(b.data, "mytool: unexpected argument '%s%n%%'\n"
			  TRY_MYTOOL);
}

TEST(error_text_len)
{
	arg_error e = mk(ARG_E_UNKNOWN_OPTION, NULL, "--jobs=4", NULL);
	struct test_buf b;

	e.text_len = 6;
	CHECK_INT(render(&b, &e, &mytool, NULL), 0);
	CHECK_STR(b.data, "mytool: unknown option '--jobs'\n" TRY_MYTOOL);

	/* an embedded NUL inside text_len is shown, not a terminator */
	e.text = "ab\0cd";
	e.text_len = 5;
	CHECK_INT(render(&b, &e, &mytool, NULL), 0);
	CHECK_STR(b.data, "mytool: unknown option 'ab\\x00cd'\n" TRY_MYTOOL);
}

TEST(error_suggestion_prefixes)
{
	arg_error e = mk(ARG_E_UNKNOWN_OPTION, NULL, "-k", NULL);
	struct test_buf b;

	e.suggestion = "j";
	e.suggestion_prefix = "-";
	CHECK_INT(render(&b, &e, &mytool, NULL), 0);
	CHECK_STR(b.data, "mytool: unknown option '-k'\n"
			  "  Did you mean '-j'?\n" TRY_MYTOOL);

	e.suggestion_prefix = "";
	CHECK_INT(render(&b, &e, &mytool, NULL), 0);
	CHECK_STR(b.data, "mytool: unknown option '-k'\n"
			  "  Did you mean 'j'?\n" TRY_MYTOOL);
}

TEST(error_suggestion_without_try_line)
{
	arg_error e = mk(ARG_E_IO, NULL, "@args", NULL);
	struct test_buf b;

	e.suggestion = "args.txt";
	CHECK_INT(render(&b, &e, &mytool, NULL), 0);
	CHECK_STR(b.data, "mytool: cannot read '@args'\n"
			  "  Did you mean 'args.txt'?\n");
}

TEST(error_path_fallbacks)
{
	static const struct arg__path root_only = { NULL, cmds3, 1 };
	static const struct arg__path sub = { NULL, cmds3, 3 };
	arg_error e = mk(ARG_E_MISSING_COMMAND, NULL, NULL, NULL);
	struct test_buf b;

	CHECK_INT(render(&b, &e, NULL, NULL), 0);
	CHECK_STR(b.data, "?: missing command\n"
			  "Try '? --help' for more information.\n");
	CHECK_INT(render(&b, &e, &root_only, NULL), 0);
	CHECK_STR(b.data, "pkg: missing command\n"
			  "Try 'pkg --help' for more information.\n");
	CHECK_INT(render(&b, &e, &sub, NULL), 0);
	CHECK_STR(b.data, "pkg repo add: missing command\n"
			  "Try 'pkg repo add --help' for more information.\n");
}

TEST(error_missing_item_and_text)
{
	arg_error e = mk(ARG_E_MISSING_VALUE, NULL, NULL, NULL);
	struct test_buf b;

	CHECK_INT(render(&b, &e, &mytool, NULL), 0);
	CHECK_STR(b.data, "mytool: option '?' requires a value\n" TRY_MYTOOL);

	e = mk(ARG_E_BAD_VALUE, NULL, NULL, NULL);
	CHECK_INT(render(&b, &e, &mytool, NULL), 0);
	CHECK_STR(b.data, "mytool: invalid value '' for '?'\n" TRY_MYTOOL);

	e = mk(ARG_E_UNKNOWN_COMMAND, NULL, NULL, NULL);
	CHECK_INT(render(&b, &e, &mytool, NULL), 0);
	CHECK_STR(b.data, "mytool: unknown command ''\n" TRY_MYTOOL);
}

/* ------------------------------------------------------------------ */
/* Custom message tables                                              */
/* ------------------------------------------------------------------ */

TEST(error_custom_messages)
{
	arg_messages m;
	arg_error e = mk(ARG_E_UNKNOWN_OPTION, NULL, "--job", NULL);
	struct test_buf b;

	memset(&m, 0, sizeof(m));
	m.text[ARG_MSG_UNKNOWN_OPTION] = "unbekannte Option '%s'";
	m.text[ARG_MSG_DID_YOU_MEAN] = "Meinten Sie '%s'?";
	m.text[ARG_MSG_TRY_HELP] = "Mehr mit '%s --help'.";
	e.suggestion = "jobs";
	e.suggestion_prefix = "--";
	CHECK_INT(render(&b, &e, &mytool, &m), 0);
	CHECK_STR(b.data, "mytool: unbekannte Option '--job'\n"
			  "  Meinten Sie '--jobs'?\n"
			  "Mehr mit 'mytool --help'.\n");

	/* entries left NULL fall back to the English defaults */
	e = mk(ARG_E_AMBIGUOUS, NULL, "--v", NULL);
	CHECK_INT(render(&b, &e, &mytool, &m), 0);
	CHECK_STR(b.data, "mytool: option '--v' is ambiguous\n"
			  "Mehr mit 'mytool --help'.\n");
}

TEST(error_custom_messages_slots)
{
	arg_messages m;
	arg_error e = mk(ARG_E_BAD_VALUE, &jobs, "abc", "kein Integer");
	struct test_buf b;

	memset(&m, 0, sizeof(m));
	/* reordered text, extra and unknown directives */
	m.text[ARG_MSG_BAD_VALUE] = "%s: '%s' 100%% falsch %d %s";
	m.text[ARG_MSG_TRY_HELP] = "Hilfe gibt es mit --help.";
	m.text[ARG_MSG_RULE] = "Regel: %s!";
	CHECK_INT(render(&b, &e, &mytool, &m), 0);
	CHECK_STR(b.data, "mytool: abc: '--jobs <N>' 100% falsch %d : "
			  "kein Integer\nHilfe gibt es mit --help.\n");

	/* a template without %s drops the arguments */
	m.text[ARG_MSG_BAD_VALUE] = "falscher Wert";
	CHECK_INT(render(&b, &e, &mytool, &m), 0);
	CHECK_STR(b.data, "mytool: falscher Wert: kein Integer\n"
			  "Hilfe gibt es mit --help.\n");

	e = mk(ARG_E_RULE, NULL, NULL, "a und b");
	CHECK_INT(render(&b, &e, &mytool, &m), 0);
	CHECK_STR(b.data, "mytool: Regel: a und b!\n"
			  "Hilfe gibt es mit --help.\n");
}

/* ------------------------------------------------------------------ */
/* Write error propagation for complete reports                       */
/* ------------------------------------------------------------------ */

struct err_ctx {
	arg_error e;
	const struct arg__path *path;
};

static int op_error(arg_write_fn w, void *ud, const void *ctx)
{
	const struct err_ctx *c = ctx;

	return arg__write_error(w, ud, &c->e, c->path, NULL);
}

TEST(error_propagates_write_errors)
{
	static const struct arg__path sub = { "mytool", cmds3, 3 };
	static const struct err_ctx cases[] = {
		{ { ARG_E_UNKNOWN_OPTION, 1, 0, NULL, NULL, "--j\x01", 0,
		    "jobs", "detail", "--" }, &sub },
		{ { ARG_E_BAD_VALUE, 1, 0, NULL, &color, "x", 0, NULL,
		    "detail", NULL }, &sub },
		{ { ARG_E_TOO_FEW_ARGS, 1, 0, NULL, &files, NULL, 0, NULL,
		    NULL, NULL }, &sub },
		{ { ARG_E_IO, 1, 0, NULL, NULL, "f", 0, "g", NULL, NULL },
		  &sub },
		{ { ARG_E_ACTION, 1, 0, NULL, NULL, NULL, 0, NULL, "boom",
		    NULL }, &sub },
		{ { (arg_status)99, 1, 0, NULL, NULL, NULL, 0, NULL, NULL,
		    NULL }, &sub },
	};
	size_t i;

	for (i = 0; i < ARG__COUNTOF(cases); i++)
		check_failures(t_, op_error, &cases[i]);
}

TEST_MAIN(TEST_CASE(status_names),
	  TEST_CASE(status_name_out_of_range),
	  TEST_CASE(exit_codes),
	  TEST_CASE(default_messages_complete),
	  TEST_CASE(default_messages_text),
	  TEST_CASE(msg_custom_table_and_fallback),
	  TEST_CASE(format_basic),
	  TEST_CASE(format_percent),
	  TEST_CASE(format_unknown_directives_are_literal),
	  TEST_CASE(format_missing_and_null_args),
	  TEST_CASE(format_length_is_honoured),
	  TEST_CASE(format_untrusted_is_escaped),
	  TEST_CASE(format_writes_runs),
	  TEST_CASE(format_propagates_write_errors),
	  TEST_CASE(escape_table),
	  TEST_CASE(escape_null_and_partial),
	  TEST_CASE(escape_writes_runs),
	  TEST_CASE(escape_propagates_write_errors),
	  TEST_CASE(path_writing),
	  TEST_CASE(path_propagates_write_errors),
	  TEST_CASE(item_labels),
	  TEST_CASE(label_propagates_write_errors),
	  TEST_CASE(error_unknown_option_with_suggestion),
	  TEST_CASE(error_golden_all_statuses),
	  TEST_CASE(error_success_statuses_print_nothing),
	  TEST_CASE(error_escapes_user_text),
	  TEST_CASE(error_text_len),
	  TEST_CASE(error_suggestion_prefixes),
	  TEST_CASE(error_suggestion_without_try_line),
	  TEST_CASE(error_path_fallbacks),
	  TEST_CASE(error_missing_item_and_text),
	  TEST_CASE(error_custom_messages),
	  TEST_CASE(error_custom_messages_slots),
	  TEST_CASE(error_propagates_write_errors))
