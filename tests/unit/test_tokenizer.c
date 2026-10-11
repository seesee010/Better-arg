// SPDX-License-Identifier: GPL-2.0-only
/*
 * test_tokenizer.c - Level 2 pull tokenizer (SDD 6.8, 7.1-7.3)
 */
#include "arg.h"
#include "test.h"

#define MAX_ARGS 16
#define COUNTOF(a) (sizeof(a) / sizeof((a)[0]))

/* argv[0] is always "prog"; args lists argv[1..] up to the first NULL. */
struct tok_case {
	const char *args[MAX_ARGS];
	uint32_t style;
	const char *value_shorts;  /* shorts that call take_value()    */
	const char *attach_shorts; /* shorts that call take_attached() */
	const char *value_longs;   /* ",name,..." calling take_value() */
	const char *want;
};

/*
 * A writable copy of an argument vector plus a snapshot used to prove
 * that the tokenizer never writes to it.
 */
struct argv_buf {
	char storage[1024];
	char *argv[MAX_ARGS + 2];
	char *orig_ptr[MAX_ARGS + 2];
	char orig[1024];
	size_t used;
	int argc;
};

static void argv_build(struct argv_buf *b, const char *const *args)
{
	int i;

	b->used = 0;
	b->argc = 0;
	memset(b->argv, 0, sizeof(b->argv));
	for (i = -1; i < MAX_ARGS; i++) {
		const char *s = i < 0 ? "prog" : args[i];
		size_t n;

		if (s == NULL)
			break;
		n = strlen(s) + 1;
		memcpy(b->storage + b->used, s, n);
		b->argv[b->argc++] = b->storage + b->used;
		b->used += n;
	}
	memcpy(b->orig, b->storage, b->used);
	memcpy(b->orig_ptr, b->argv, sizeof(b->argv));
}

static bool argv_unchanged(const struct argv_buf *b)
{
	return memcmp(b->orig, b->storage, b->used) == 0 &&
	       memcmp(b->orig_ptr, b->argv, sizeof(b->argv)) == 0;
}

static void out_add(char *out, size_t cap, const char *fmt, ...)
	TEST_PRINTF_(3, 4);

static void out_add(char *out, size_t cap, const char *fmt, ...)
{
	size_t len = strlen(out);
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(out + len, cap - len, fmt, ap);
	va_end(ap);
}

static bool long_takes_value(const char *list, const char *name,
			     size_t len)
{
	const char *p;

	if (list == NULL)
		return false;
	for (p = strchr(list, ','); p != NULL; p = strchr(p + 1, ',')) {
		if (strncmp(p + 1, name, len) == 0 && p[1 + len] == ',')
			return true;
	}
	return false;
}

/*
 * Tokenize everything and render it as text:
 *   SHORT       -c@ARGI.OFFSET, then +VALUE (take_value) or ~VALUE
 *               (take_attached) when the case asks for it
 *   LONG        --name[=inline]@ARGI, then +VALUE when taken
 *   POSITIONAL  P(value)@ARGI
 *   END         END@ARGI
 * A NULL value is rendered as NULL.
 */
static void render(arg_tokenizer *tz, const struct tok_case *c, char *out,
		   size_t cap)
{
	arg_tok t;
	const char *v;

	out[0] = '\0';
	while (arg_tok_next(tz, &t)) {
		if (out[0] != '\0')
			out_add(out, cap, " ");
		switch (t.kind) {
		case ARG_T_SHORT:
			out_add(out, cap, "-%c@%d.%u", t.ch, t.argi,
				(unsigned)t.offset);
			if (c->value_shorts && strchr(c->value_shorts, t.ch)) {
				v = arg_tok_take_value(tz);
				out_add(out, cap, "+%s", v ? v : "NULL");
			} else if (c->attach_shorts &&
				   strchr(c->attach_shorts, t.ch)) {
				v = arg_tok_take_attached(tz);
				out_add(out, cap, "~%s", v ? v : "NULL");
			}
			break;
		case ARG_T_LONG:
			out_add(out, cap, "--%.*s", (int)t.name_len, t.name);
			if (t.value)
				out_add(out, cap, "=%s", t.value);
			out_add(out, cap, "@%d", t.argi);
			if (!t.value && long_takes_value(c->value_longs, t.name,
							 t.name_len)) {
				v = arg_tok_take_value(tz);
				out_add(out, cap, "+%s", v ? v : "NULL");
			}
			break;
		case ARG_T_POSITIONAL:
			out_add(out, cap, "P(%s)@%d", t.value, t.argi);
			break;
		case ARG_T_END:
			out_add(out, cap, "END@%d", t.argi);
			break;
		default:
			out_add(out, cap, "?");
			break;
		}
	}
}

#define POSIX ARG_STYLE_POSIX
#define NEG ARG_TOK_NEGATIVE_NUMBERS

static const struct tok_case cases[] = {
	/* SDD 7.3, syntax-level rows */
	{ { "-vvv" }, 0, 0, 0, 0, "-v@1.1 -v@1.2 -v@1.3" },
	{ { "-j4" }, 0, "j", 0, 0, "-j@1.1+4" },
	{ { "-j", "-4" }, 0, "j", 0, 0, "-j@1.1+-4" },
	{ { "-xvf", "a.tar" }, 0, "f", 0, 0,
	  "-x@1.1 -v@1.2 -f@1.3+a.tar" },
	{ { "--out=" }, 0, 0, 0, ",out,", "--out=@1" },
	{ { "--jobs=" }, 0, 0, 0, ",jobs,", "--jobs=@1" },
	{ { "--", "-v" }, 0, 0, 0, 0, "END@1 P(-v)@2" },
	{ { "-" }, 0, 0, 0, 0, "P(-)@1" },
	{ { "-5" }, 0, 0, 0, 0, "-5@1.1" },
	{ { "-5" }, NEG, 0, 0, 0, "P(-5)@1" },
	{ { "--verb" }, 0, 0, 0, 0, "--verb@1" },
	{ { "--color", "red" }, 0, 0, 0, 0, "--color@1 P(red)@2" },
	{ { "--no-color" }, 0, 0, 0, 0, "--no-color@1" },

	/* short bundles and values */
	{ { "-a" }, 0, 0, 0, 0, "-a@1.1" },
	{ { "-abc" }, 0, 0, 0, 0, "-a@1.1 -b@1.2 -c@1.3" },
	{ { "-ofile" }, 0, "o", 0, 0, "-o@1.1+file" },
	{ { "-vofile" }, 0, "o", 0, 0, "-v@1.1 -o@1.2+file" },
	{ { "-abc", "x" }, 0, "b", 0, 0, "-a@1.1 -b@1.2+c P(x)@2" },
	{ { "-abc", "x" }, 0, "c", 0, 0, "-a@1.1 -b@1.2 -c@1.3+x" },
	{ { "-o", "file", "x" }, 0, "o", 0, 0, "-o@1.1+file P(x)@3" },
	{ { "-vo", "f", "-x" }, 0, "o", 0, 0, "-v@1.1 -o@1.2+f -x@3.1" },
	{ { "-o" }, 0, "o", 0, 0, "-o@1.1+NULL" },
	{ { "-vo" }, 0, "o", 0, 0, "-v@1.1 -o@1.2+NULL" },
	{ { "-o", "--", "-v" }, 0, "o", 0, 0, "-o@1.1+-- -v@3.1" },
	{ { "-o", "--x" }, 0, "o", 0, 0, "-o@1.1+--x" },
	{ { "-o", "-" }, 0, "o", 0, 0, "-o@1.1+-" },
	{ { "-o", "" }, 0, "o", 0, 0, "-o@1.1+" },
	{ { "-o-x" }, 0, "o", 0, 0, "-o@1.1+-x" },
	{ { "-oo", "x" }, 0, "o", 0, 0, "-o@1.1+o P(x)@2" },
	{ { "-o", "-o", "x" }, 0, "o", 0, 0, "-o@1.1+-o P(x)@3" },
	{ { "-a-b" }, 0, 0, 0, 0, "-a@1.1 --@1.2 -b@1.3" },
	{ { "-a=b" }, 0, 0, 0, 0, "-a@1.1 -=@1.2 -b@1.3" },

	/* take_attached: only the rest of the bundle */
	{ { "-calways" }, 0, 0, "c", 0, "-c@1.1~always" },
	{ { "-vcalways", "x" }, 0, 0, "c", 0, "-v@1.1 -c@1.2~always P(x)@2" },
	{ { "-c", "always" }, 0, 0, "c", 0, "-c@1.1~NULL P(always)@2" },
	{ { "-vc", "x" }, 0, 0, "c", 0, "-v@1.1 -c@1.2~NULL P(x)@2" },
	{ { "-cv" }, 0, 0, "c", 0, "-c@1.1~v" },

	/* long options */
	{ { "--out", "file" }, 0, 0, 0, ",out,", "--out@1+file" },
	{ { "--out" }, 0, 0, 0, ",out,", "--out@1+NULL" },
	{ { "--out", "-x" }, 0, 0, 0, ",out,", "--out@1+-x" },
	{ { "--out", "--" }, 0, 0, 0, ",out,", "--out@1+--" },
	{ { "--out", "--", "-v" }, 0, 0, 0, ",out,", "--out@1+-- -v@3.1" },
	{ { "--out=a", "b" }, 0, 0, 0, ",out,", "--out=a@1 P(b)@2" },
	{ { "--out", "file" }, 0, 0, 0, 0, "--out@1 P(file)@2" },
	{ { "--a=b=c" }, 0, 0, 0, 0, "--a=b=c@1" },
	{ { "--=x" }, 0, 0, 0, 0, "--=x@1" },
	{ { "---x" }, 0, 0, 0, 0, "---x@1" },
	{ { "--x", "--y=1", "--z" }, 0, 0, 0, 0, "--x@1 --y=1@2 --z@3" },
	{ { "--5" }, NEG, 0, 0, 0, "--5@1" },

	/* "--" */
	{ { "--" }, 0, 0, 0, 0, "END@1" },
	{ { "--", "--" }, 0, 0, 0, 0, "END@1 P(--)@2" },
	{ { "--", "-", "--x", "-abc", "a" }, 0, 0, 0, 0,
	  "END@1 P(-)@2 P(--x)@3 P(-abc)@4 P(a)@5" },
	{ { "-v", "--", "-5" }, NEG, 0, 0, 0, "-v@1.1 END@2 P(-5)@3" },

	/* positionals and GNU interleaving */
	{ { "" }, 0, 0, 0, 0, "P()@1" },
	{ { "a", "-v", "b", "--x", "c" }, 0, 0, 0, 0,
	  "P(a)@1 -v@2.1 P(b)@3 --x@4 P(c)@5" },
	{ { "a", "--", "-v" }, 0, 0, 0, 0, "P(a)@1 END@2 P(-v)@3" },

	/* POSIX ordering */
	{ { "-v", "a", "-w", "--", "--x" }, POSIX, 0, 0, 0,
	  "-v@1.1 P(a)@2 P(-w)@3 P(--)@4 P(--x)@5" },
	{ { "--", "-v", "a" }, POSIX, 0, 0, 0, "END@1 P(-v)@2 P(a)@3" },
	{ { "-o", "a", "-v", "b", "-w" }, POSIX, "o", 0, 0,
	  "-o@1.1+a -v@3.1 P(b)@4 P(-w)@5" },
	{ { "--out", "a", "-v" }, POSIX, 0, 0, ",out,",
	  "--out@1+a -v@3.1" },
	{ { "-", "-v" }, POSIX, 0, 0, 0, "P(-)@1 P(-v)@2" },
	{ { "-5", "-v" }, POSIX | NEG, 0, 0, 0, "P(-5)@1 P(-v)@2" },

	/* negative numbers */
	{ { "-5.3", "-1e3", "-0" }, NEG, 0, 0, 0,
	  "P(-5.3)@1 P(-1e3)@2 P(-0)@3" },
	{ { "-9x" }, NEG, 0, 0, 0, "P(-9x)@1" },
	{ { "-x5" }, NEG, 0, 0, 0, "-x@1.1 -5@1.2" },
	{ { "-a" }, NEG, 0, 0, 0, "-a@1.1" },
	{ { "-" }, NEG, 0, 0, 0, "P(-)@1" },
	{ { "-j", "-4" }, NEG, "j", 0, 0, "-j@1.1+-4" },
	{ { "-12" }, 0, 0, 0, 0, "-1@1.1 -2@1.2" },
	{ { "-/", "-:" }, NEG, 0, 0, 0, "-/@1.1 -:@2.1" },

	/* bytes in, bytes out */
	{ { "-\xc3\xa4" }, 0, 0, 0, 0, "-\xc3@1.1 -\xa4@1.2" },
	{ { "--na\xc3\xafve=\xff" }, 0, 0, 0, 0, "--na\xc3\xafve=\xff@1" },
	{ { "-o\xe2\x82\xac" }, 0, "o", 0, 0, "-o@1.1+\xe2\x82\xac" },
	{ { "\xff\xfe" }, 0, 0, 0, 0, "P(\xff\xfe)@1" },
};

TEST(table)
{
	size_t i;

	for (i = 0; i < COUNTOF(cases); i++) {
		struct argv_buf b;
		arg_tokenizer tz;
		char got[1024];
		arg_tok t;

		argv_build(&b, cases[i].args);
		arg_tok_init(&tz, b.argc, b.argv, cases[i].style);
		render(&tz, &cases[i], got, sizeof(got));
		t_->checks++;
		if (strcmp(got, cases[i].want) != 0)
			TEST_FAIL_("case %u: got \"%s\", want \"%s\"",
				   (unsigned)i, got, cases[i].want);
		CHECK(!arg_tok_next(&tz, &t));
		CHECK(argv_unchanged(&b));
	}
}

static void init_args(struct argv_buf *b, arg_tokenizer *tz, uint32_t style,
		      const char *const *args)
{
	argv_build(b, args);
	arg_tok_init(tz, b->argc, b->argv, style);
}

TEST(init_state)
{
	static const char *const args[] = { "-v", NULL };
	struct argv_buf b;
	arg_tokenizer tz;

	init_args(&b, &tz, POSIX | NEG, args);
	CHECK_INT(tz.argc, 2);
	CHECK(tz.argv == b.argv);
	CHECK_UINT(tz.style, POSIX | NEG);
	CHECK_INT(tz.argi, 1);
	CHECK_UINT(tz.pos, 0);
	CHECK(!tz.after_dashdash);
	CHECK(!tz.posix_stopped);
}

TEST(empty_and_malformed_argv)
{
	static const int argcs[] = { 0, -1, -1000, 1 };
	char prog[] = "prog", x[] = "-x";
	char *argv[] = { prog, x, NULL };
	char *null0[] = { NULL, x, NULL };
	arg_tokenizer tz;
	arg_tok t;
	size_t i;

	for (i = 0; i < COUNTOF(argcs); i++) {
		arg_tok_init(&tz, argcs[i], argv, 0);
		CHECK(!arg_tok_next(&tz, &t));
		CHECK(!arg_tok_next(&tz, &t));
		CHECK(arg_tok_take_value(&tz) == NULL);
		CHECK(arg_tok_take_attached(&tz) == NULL);
	}
	CHECK_INT(tz.argc, 1);

	arg_tok_init(&tz, -5, argv, 0);
	CHECK_INT(tz.argc, 0);

	/* No argv at all. */
	arg_tok_init(&tz, 3, NULL, 0);
	CHECK(!arg_tok_next(&tz, &t));
	CHECK(arg_tok_take_value(&tz) == NULL);

	/* argv[0] == NULL ends the iteration before it starts. */
	arg_tok_init(&tz, 2, null0, 0);
	CHECK(!arg_tok_next(&tz, &t));
	CHECK(!arg_tok_next(&tz, &t));
}

TEST(null_entry_ends_iteration)
{
	char prog[] = "prog", v[] = "-v", o[] = "-o", x[] = "x";
	char *argv[] = { prog, v, NULL, x, NULL };
	char *argv2[] = { prog, o, NULL, x, NULL };
	arg_tokenizer tz;
	arg_tok t;

	arg_tok_init(&tz, 4, argv, 0);
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.kind, ARG_T_SHORT);
	CHECK(!arg_tok_next(&tz, &t));
	CHECK(!arg_tok_next(&tz, &t));
	CHECK(arg_tok_take_value(&tz) == NULL);
	CHECK(!arg_tok_next(&tz, &t));

	/* take_value does not read past a NULL entry. */
	arg_tok_init(&tz, 4, argv2, 0);
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.ch, 'o');
	CHECK(arg_tok_take_value(&tz) == NULL);
	CHECK(!arg_tok_next(&tz, &t));
	CHECK(!arg_tok_next(&tz, &t));
}

TEST(argc_limits_iteration)
{
	static const char *const args[] = { "a", "b", "c", NULL };
	struct argv_buf b;
	arg_tokenizer tz;
	arg_tok t;

	argv_build(&b, args);
	arg_tok_init(&tz, 2, b.argv, 0);
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_STR(t.value, "a");
	CHECK(!arg_tok_next(&tz, &t));

	/* take_value does not read past argc either. */
	{
		static const char *const args2[] = { "--out", "x", NULL };

		argv_build(&b, args2);
		arg_tok_init(&tz, 2, b.argv, 0);
		REQUIRE(arg_tok_next(&tz, &t));
		CHECK_INT(t.kind, ARG_T_LONG);
		CHECK(arg_tok_take_value(&tz) == NULL);
		CHECK(!arg_tok_next(&tz, &t));
	}
}

TEST(repeated_next_after_end)
{
	static const char *const args[] = { "-ab", "--x", "y", NULL };
	struct argv_buf b;
	arg_tokenizer tz;
	arg_tok t;
	int n = 0, i;

	init_args(&b, &tz, 0, args);
	while (arg_tok_next(&tz, &t))
		n++;
	CHECK_INT(n, 4);
	for (i = 0; i < 5; i++) {
		CHECK(!arg_tok_next(&tz, &t));
		CHECK(arg_tok_take_value(&tz) == NULL);
		CHECK(arg_tok_take_attached(&tz) == NULL);
	}
	CHECK_INT(tz.argi, 4);
	CHECK(argv_unchanged(&b));
}

TEST(token_fields)
{
	static const char *const args[] = { "-ab", "--name=val", "--flag",
					    "pos", "--", NULL };
	struct argv_buf b;
	arg_tokenizer tz;
	arg_tok t;

	init_args(&b, &tz, 0, args);

	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.kind, ARG_T_SHORT);
	CHECK_INT(t.argi, 1);
	CHECK_UINT(t.offset, 1);
	CHECK_INT(t.ch, 'a');
	CHECK(t.name == NULL);
	CHECK_UINT(t.name_len, 0);
	CHECK(t.value == NULL);

	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.kind, ARG_T_SHORT);
	CHECK_INT(t.argi, 1);
	CHECK_UINT(t.offset, 2);
	CHECK_INT(t.ch, 'b');
	CHECK(t.name == NULL && t.value == NULL);

	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.kind, ARG_T_LONG);
	CHECK_INT(t.argi, 2);
	CHECK_UINT(t.offset, 0);
	CHECK_INT(t.ch, 0);
	CHECK(t.name == b.argv[2] + 2);
	CHECK_STRN(t.name, t.name_len, "name");
	CHECK(t.value == b.argv[2] + 7);
	CHECK_STR(t.value, "val");

	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.kind, ARG_T_LONG);
	CHECK_INT(t.argi, 3);
	CHECK_STRN(t.name, t.name_len, "flag");
	CHECK(t.value == NULL);

	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.kind, ARG_T_POSITIONAL);
	CHECK_INT(t.argi, 4);
	CHECK_UINT(t.offset, 0);
	CHECK(t.value == b.argv[4]);
	CHECK(t.name == NULL);

	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.kind, ARG_T_END);
	CHECK_INT(t.argi, 5);
	CHECK_UINT(t.offset, 0);
	CHECK(t.name == NULL && t.value == NULL);
	CHECK(tz.after_dashdash);

	CHECK(!arg_tok_next(&tz, &t));
	CHECK(argv_unchanged(&b));
}

TEST(take_value_points_into_argv)
{
	static const char *const args[] = { "-vofile", "-o", "next", NULL };
	struct argv_buf b;
	arg_tokenizer tz;
	arg_tok t;

	init_args(&b, &tz, 0, args);
	REQUIRE(arg_tok_next(&tz, &t));
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK(arg_tok_take_value(&tz) == b.argv[1] + 3);
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.argi, 2);
	CHECK(arg_tok_take_value(&tz) == b.argv[3]);
	CHECK_INT(tz.argi, 4);
	CHECK(!arg_tok_next(&tz, &t));
}

TEST(take_value_without_option)
{
	static const char *const args[] = { "--a=1", "x", "--", "y",
					    "z", NULL };
	struct argv_buf b;
	arg_tokenizer tz;
	arg_tok t;

	init_args(&b, &tz, 0, args);
	/* Before any token. */
	CHECK(arg_tok_take_value(&tz) == NULL);
	CHECK(arg_tok_take_attached(&tz) == NULL);

	/* LONG with an inline value. */
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.kind, ARG_T_LONG);
	CHECK(arg_tok_take_value(&tz) == NULL);
	CHECK(arg_tok_take_attached(&tz) == NULL);

	/* POSITIONAL. */
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_STR(t.value, "x");
	CHECK(arg_tok_take_value(&tz) == NULL);
	CHECK(arg_tok_take_attached(&tz) == NULL);

	/* END. */
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.kind, ARG_T_END);
	CHECK(arg_tok_take_value(&tz) == NULL);
	CHECK(arg_tok_take_attached(&tz) == NULL);

	/* Nothing above consumed an argument. */
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_STR(t.value, "y");
	CHECK_INT(t.argi, 4);
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_STR(t.value, "z");
	CHECK(!arg_tok_next(&tz, &t));
}

TEST(take_value_twice)
{
	static const char *const args[] = { "--out", "a", "b", "-ob", "c",
					    NULL };
	struct argv_buf b;
	arg_tokenizer tz;
	arg_tok t;

	init_args(&b, &tz, 0, args);
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_STR(arg_tok_take_value(&tz), "a");
	CHECK(arg_tok_take_value(&tz) == NULL);
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_STR(t.value, "b");

	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.ch, 'o');
	CHECK_STR(arg_tok_take_value(&tz), "b");
	CHECK(arg_tok_take_value(&tz) == NULL);
	CHECK(arg_tok_take_attached(&tz) == NULL);
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_STR(t.value, "c");
	CHECK(!arg_tok_next(&tz, &t));
}

TEST(take_attached_never_consumes_next)
{
	static const char *const args[] = { "--color", "red", "-c", "-v",
					    NULL };
	struct argv_buf b;
	arg_tokenizer tz;
	arg_tok t;

	init_args(&b, &tz, 0, args);
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.kind, ARG_T_LONG);
	CHECK(arg_tok_take_attached(&tz) == NULL);
	/* The long option is still open: take_value still works. */
	CHECK_STR(arg_tok_take_value(&tz), "red");

	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.ch, 'c');
	CHECK(arg_tok_take_attached(&tz) == NULL);
	CHECK(arg_tok_take_attached(&tz) == NULL);
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.kind, ARG_T_SHORT);
	CHECK_INT(t.ch, 'v');
	CHECK_INT(t.argi, 4);
	CHECK(!arg_tok_next(&tz, &t));
}

TEST(take_attached_finishes_bundle)
{
	static const char *const args[] = { "-abc", "-d", NULL };
	struct argv_buf b;
	arg_tokenizer tz;
	arg_tok t;

	init_args(&b, &tz, 0, args);
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.ch, 'a');
	CHECK(arg_tok_take_attached(&tz) == b.argv[1] + 2);
	CHECK(arg_tok_take_attached(&tz) == NULL);
	CHECK(arg_tok_take_value(&tz) == NULL);
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.ch, 'd');
	CHECK_INT(t.argi, 2);
	CHECK(!arg_tok_next(&tz, &t));
}

TEST(posix_stop_and_resume)
{
	static const char *const args[] = { "-v", "sub", "-w", "x", "-y",
					    NULL };
	struct argv_buf b;
	arg_tokenizer tz;
	arg_tok t;

	init_args(&b, &tz, POSIX, args);
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.kind, ARG_T_SHORT);
	CHECK(!tz.posix_stopped);
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.kind, ARG_T_POSITIONAL);
	CHECK(tz.posix_stopped);

	/* The parser saw a subcommand name. */
	arg_tok_resume_options(&tz);
	CHECK(!tz.posix_stopped);
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.kind, ARG_T_SHORT);
	CHECK_INT(t.ch, 'w');
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.kind, ARG_T_POSITIONAL);
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.kind, ARG_T_POSITIONAL);
	CHECK_STR(t.value, "-y");
	CHECK(!arg_tok_next(&tz, &t));
}

TEST(resume_never_undoes_dashdash)
{
	static const char *const args[] = { "--", "sub", "-v", NULL };
	uint32_t styles[] = { 0, POSIX };
	size_t i;

	for (i = 0; i < COUNTOF(styles); i++) {
		struct argv_buf b;
		arg_tokenizer tz;
		arg_tok t;

		init_args(&b, &tz, styles[i], args);
		REQUIRE(arg_tok_next(&tz, &t));
		CHECK_INT(t.kind, ARG_T_END);
		REQUIRE(arg_tok_next(&tz, &t));
		CHECK_INT(t.kind, ARG_T_POSITIONAL);
		arg_tok_resume_options(&tz);
		CHECK(tz.after_dashdash);
		REQUIRE(arg_tok_next(&tz, &t));
		CHECK_INT(t.kind, ARG_T_POSITIONAL);
		CHECK_STR(t.value, "-v");
		CHECK(!arg_tok_next(&tz, &t));
	}
}

TEST(gnu_never_stops)
{
	static const char *const args[] = { "a", "b", "-v", NULL };
	struct argv_buf b;
	arg_tokenizer tz;
	arg_tok t;

	init_args(&b, &tz, 0, args);
	REQUIRE(arg_tok_next(&tz, &t));
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK(!tz.posix_stopped);
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.kind, ARG_T_SHORT);
}

TEST(style_read_on_every_call)
{
	static const char *const args[] = { "-5", "-5", "-5", "a", "-v",
					    NULL };
	struct argv_buf b;
	arg_tokenizer tz;
	arg_tok t;

	init_args(&b, &tz, 0, args);
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.kind, ARG_T_SHORT);
	CHECK_INT(t.ch, '5');
	tz.style |= NEG;
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.kind, ARG_T_POSITIONAL);
	CHECK_STR(t.value, "-5");
	tz.style &= ~NEG;
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.kind, ARG_T_SHORT);

	/* Switching to POSIX takes effect at the next positional. */
	tz.style |= POSIX;
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.kind, ARG_T_POSITIONAL);
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.kind, ARG_T_POSITIONAL);
	CHECK_STR(t.value, "-v");
}

TEST(long_name_is_not_terminated)
{
	static const char *const args[] = { "--key=v", "--=", NULL };
	struct argv_buf b;
	arg_tokenizer tz;
	arg_tok t;

	init_args(&b, &tz, 0, args);
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_UINT(t.name_len, 3);
	CHECK_INT(t.name[t.name_len], '=');
	CHECK_STR(t.value, "v");
	REQUIRE(arg_tok_next(&tz, &t));
	CHECK_INT(t.kind, ARG_T_LONG);
	CHECK_UINT(t.name_len, 0);
	CHECK_STR(t.value, "");
}

TEST_MAIN(TEST_CASE(table),
	  TEST_CASE(init_state),
	  TEST_CASE(empty_and_malformed_argv),
	  TEST_CASE(null_entry_ends_iteration),
	  TEST_CASE(argc_limits_iteration),
	  TEST_CASE(repeated_next_after_end),
	  TEST_CASE(token_fields),
	  TEST_CASE(take_value_points_into_argv),
	  TEST_CASE(take_value_without_option),
	  TEST_CASE(take_value_twice),
	  TEST_CASE(take_attached_never_consumes_next),
	  TEST_CASE(take_attached_finishes_bundle),
	  TEST_CASE(posix_stop_and_resume),
	  TEST_CASE(resume_never_undoes_dashdash),
	  TEST_CASE(gnu_never_stops),
	  TEST_CASE(style_read_on_every_call),
	  TEST_CASE(long_name_is_not_terminated))
