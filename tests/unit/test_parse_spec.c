// SPDX-License-Identifier: GPL-2.0-only
/*
 * test_parse_spec.c - the spec compiler (SDD 5.2, 5.3, 12.3),
 * arg_compile() and arg_foreach_item()
 */
#include "parse_util.h"

#include <stdlib.h>

static int run(arg *a)
{
	(void)a;
	return 0;
}

static int on_each(arg *a, const char *v)
{
	(void)a;
	(void)v;
	return 0;
}

static int nop_parse(const arg_type *t, const char *s, size_t n, void *dst,
		     arg_ctx *ctx)
{
	(void)t;
	(void)s;
	(void)n;
	(void)dst;
	(void)ctx;
	return 0;
}

static const arg_type t_noparse = { .name = "X", .size = 4 };
static const arg_type t_nosize = { .name = "X", .parse = nop_parse };
static const arg_type t_custom = { .name = "X", .size = 4,
				   .parse = nop_parse };

/*
 * Both arg_compile() and arg_parse() (for errors at the root) must
 * report ARG_E_SPEC with this detail and offending item.
 */
static void expect(struct test_state *t_, const arg_item *spec,
		   const char *detail, const arg_item *item, bool at_root)
{
	arg_error err;
	struct fx f;

	memset(&err, 0xff, sizeof(err));
	CHECK_INT(arg_compile(spec, NULL, &err), ARG_E_SPEC);
	CHECK_INT(err.status, ARG_E_SPEC);
	CHECK_STR(err.detail, detail);
	CHECK(err.item == item);
	CHECK_INT(err.argi, -1);
	if (!at_root)
		return;
	fx_init(&f, spec);
	CHECK_INT(PARSE0(&f), ARG_E_SPEC);
	CHECK_STR(fx_error(&f)->detail, detail);
	CHECK(fx_error(&f)->item == item);
	CHECK(fx_error(&f)->cmd == spec);
}

#define EXPECT(spec, detail, item) expect(t_, spec, detail, item, true)

TEST(root)
{
	const arg_item flag[] = { ARG_FLAG('v', "v", "V"), ARG__END };
	arg_error err;
	struct fx f;

	CHECK_INT(arg_compile(NULL, NULL, NULL), ARG_E_SPEC);
	CHECK_INT(arg_compile(flag, NULL, &err), ARG_E_SPEC);
	CHECK_STR(err.detail, "root must be an ARG_I_CMD item");
	fx_init(&f, flag);
	CHECK_INT(PARSE0(&f), ARG_E_SPEC);
	CHECK_STR(fx_report(&f),
		  "prog: invalid specification: root must be an ARG_I_CMD "
		  "item\n");
	CHECK_INT(arg_exit_code(ARG_E_SPEC), 70);
	fx_init(&f, NULL);
	CHECK_INT(PARSE0(&f), ARG_E_SPEC);
}

TEST(option_names)
{
	const arg_item a[] = { ARG_APP("t", "T", ARG_FLAG(0, NULL, "x")) };
	const arg_item b[] = { ARG_APP("t", "T", ARG_FLAG(0, "", "x")) };
	const arg_item c[] = { ARG_APP("t", "T", ARG_FLAG(' ', "x", "x")) };
	const arg_item d[] = { ARG_APP("t", "T", ARG_FLAG('-', "x", "x")) };
	const arg_item e[] = { ARG_APP("t", "T",
				       ARG_FLAG((char)0x80, "x", "x")) };
	const arg_item g[] = { ARG_APP("t", "T", ARG_FLAG(0, "-x", "x")) };
	const arg_item h[] = { ARG_APP("t", "T", ARG_FLAG(0, "a=b", "x")) };
	const arg_item i[] = { ARG_APP("t", "T",
				       ARG_FLAG(0, "x", "x", .alias = "")) };

	EXPECT(a, "option without a short or long name", &a[0].items[0]);
	EXPECT(b, "option without a short or long name", &b[0].items[0]);
	EXPECT(c, "invalid short option character", &c[0].items[0]);
	EXPECT(d, "invalid short option character", &d[0].items[0]);
	EXPECT(e, "invalid short option character", &e[0].items[0]);
	EXPECT(g, "invalid long option name", &g[0].items[0]);
	EXPECT(h, "invalid long option name", &h[0].items[0]);
	EXPECT(i, "invalid option alias", &i[0].items[0]);
}

TEST(functions_and_types)
{
	const arg_item a[] = { ARG_APP("t", "T", ARG_DO('d', "d", NULL, "")) };
	const arg_item b[] = { ARG_APP("t", "T", ARG_ON('o', "o", NULL, "")) };
	const arg_item c[] = { ARG_APP("t", "T", ARG_ON('o', "o", on_each, "",
							.many = 1)) };
	const arg_item d[] = { ARG_APP("t", "T",
				       ARG_CUSTOM('x', "x", &t_noparse, "")) };
	const arg_item e[] = { ARG_APP("t", "T",
				       ARG_CUSTOM('x', "x", &t_nosize, "")) };
	const arg_item g[] = { ARG_APP("t", "T",
		{ .kind = ARG_I_FLAG, .short_name = 'x',
		  .type = &arg_t_int }) };
	const arg_item h[] = { ARG_APP("t", "T",
				       ARG_CUSTOM('x', "x", &arg_t_enum, "")) };
	const arg_item i[] = { ARG_APP("t", "T", ARG_ARG("p", "",
							 .type = &t_nosize)) };

	EXPECT(a, "ARG_DO without a function", &a[0].items[0]);
	EXPECT(b, "ARG_ON without a function", &b[0].items[0]);
	EXPECT(c, "ARG_ON cannot collect a list", &c[0].items[0]);
	EXPECT(d, "type without parse function or size", &d[0].items[0]);
	EXPECT(e, "type without parse function or size", &e[0].items[0]);
	EXPECT(g, "flag type must be a bool type", &g[0].items[0]);
	EXPECT(h, "enum without choices", &h[0].items[0]);
	EXPECT(i, "type without parse function or size", &i[0].items[0]);
}

TEST(bindings)
{
	int iv;
	double dv;
	const char *sv;
	bool bv;
	uint32_t raw;
	const arg_item ok[] = { ARG_APP(
		"t", "T", ARG_FLAG('b', "b", "", .to = ARG_TO_BOOL(&bv)),
		ARG_COUNT('c', "c", "", .to = ARG_TO_INT(&iv)),
		ARG_INT(0, "i", "", .to = ARG_TO_INT(&iv)),
		ARG_ENUM(0, "e", "", ARG_CHOICES("x"), .to = ARG_TO_INT(&iv)),
		ARG_UINT(0, "u", "", .to = ARG_TO_INT(&iv)),
		ARG_DOUBLE(0, "d", "", .to = ARG_TO_DOUBLE(&dv)),
		ARG_STR(0, "s", "", .to = ARG_TO_STR(&sv)),
		ARG_CUSTOM(0, "r", &t_custom, "", .to = ARG_TO_RAW(&raw)),
		ARG_INT(0, "ri", "", .to = ARG_TO_RAW(&raw)),
		ARG_ARG("p", "", .to = ARG_TO_STR(&sv))) };
	const arg_item a[] = { ARG_APP("t", "T", ARG_INT(0, "i", "",
						 .to = ARG_TO_STR(&sv))) };
	const arg_item b[] = { ARG_APP("t", "T", ARG_STR(0, "s", "",
						 .to = ARG_TO_INT(&iv))) };
	const arg_item c[] = { ARG_APP("t", "T", ARG_INT(0, "i", "",
						 .to = ARG_TO_DOUBLE(&dv))) };
	const arg_item d[] = { ARG_APP("t", "T", ARG_INT(0, "i", "",
						 .to = ARG_TO_BOOL(&bv))) };
	const arg_item e[] = { ARG_APP("t", "T", ARG_FLAG(0, "f", "",
						  .to = ARG_TO_INT(&iv))) };
	const arg_item g[] = { ARG_APP("t", "T", ARG_DOUBLE(0, "d", "",
						    .to = ARG_TO_INT(&iv))) };
	const arg_item h[] = { ARG_APP("t", "T",
		ARG_CUSTOM(0, "x", &t_custom, "", .to = ARG_TO_INT(&iv))) };
	const arg_item i[] = { ARG_APP("t", "T", ARG_DO(0, "x", run, "",
						.to = ARG_TO_RAW(&raw))) };
	const arg_item j[] = { ARG_APP("t", "T",
		ARG_INT(0, "x", "", .to = { .ptr = NULL,
					    .type = ARG_TO_T_INT })) };
	const arg_item k[] = { ARG_APP("t", "T",
		ARG_INT(0, "x", "", .to = { .ptr = &iv,
					    .type = (arg_to_type)99 })) };
	const arg_item l[] = { ARG_APP("t", "T", ARG_COUNT(0, "c", "",
						  .to = ARG_TO_STR(&sv))) };

	CHECK_INT(arg_compile(ok, NULL, NULL), ARG_OK);
	EXPECT(a, ".to binding does not fit the item type", &a[0].items[0]);
	EXPECT(b, ".to binding does not fit the item type", &b[0].items[0]);
	EXPECT(c, ".to binding does not fit the item type", &c[0].items[0]);
	EXPECT(d, ".to binding does not fit the item type", &d[0].items[0]);
	EXPECT(e, ".to binding does not fit the item type", &e[0].items[0]);
	EXPECT(g, ".to binding does not fit the item type", &g[0].items[0]);
	EXPECT(h, ".to binding does not fit the item type", &h[0].items[0]);
	EXPECT(i, ".to binding on an item without a value", &i[0].items[0]);
	EXPECT(j, ".to binding without a variable", &j[0].items[0]);
	EXPECT(k, "invalid .to binding type", &k[0].items[0]);
	EXPECT(l, ".to binding does not fit the item type", &l[0].items[0]);
}

TEST(texts)
{
	const arg_item ok[] = { ARG_APP(
		"t", "T", ARG_INT(0, "i", "", .def = "5", .min = "-1",
				  .max = "0x10"),
		ARG_SIZE(0, "s", "", .min = "1k"),
		ARG_COUNT('v', "v", "", .def = "2", .max = "3"),
		ARG_FLAG('f', "f", "", .def = "yes"),
		ARG_INT(0, "l", "", .many = 1, .sep = ',', .def = "1,2,3"),
		ARG_ENUM(0, "e", "", ARG_CHOICES("x", "y"), .def = "y")) };
	const arg_item a[] = { ARG_APP("t", "T",
				       ARG_INT(0, "i", "", .def = "five")) };
	const arg_item b[] = { ARG_APP("t", "T",
				       ARG_INT(0, "i", "", .min = "x")) };
	const arg_item c[] = { ARG_APP("t", "T",
				       ARG_INT(0, "i", "", .max = "")) };
	const arg_item d[] = { ARG_APP("t", "T",
				       ARG_STR(0, "s", "", .min = "a")) };
	const arg_item e[] = { ARG_APP("t", "T", ARG_DO(0, "d", run, "",
							.def = "1")) };
	const arg_item g[] = { ARG_APP("t", "T", ARG_INT(0, "l", "",
		.many = 1, .sep = ',', .def = "1,x")) };
	const arg_item h[] = { ARG_APP("t", "T", ARG_ENUM(0, "e", "",
		ARG_CHOICES("x"), .def = "z")) };
	const arg_item i[] = { ARG_APP("t", "T", ARG_FLAG(0, "f", "",
		.max = "true")) };
	const arg_item j[] = { ARG_APP("t", "T",
				       ARG_ARG_OPT("p", "", .type = &arg_t_u8,
						   .def = "256")) };

	CHECK_INT(arg_compile(ok, NULL, NULL), ARG_OK);
	EXPECT(a, "default does not parse with the item's type",
	       &a[0].items[0]);
	EXPECT(b, "bound does not parse with the item's type",
	       &b[0].items[0]);
	EXPECT(c, "bound does not parse with the item's type",
	       &c[0].items[0]);
	EXPECT(d, ".min/.max need a numeric type", &d[0].items[0]);
	EXPECT(e, ".def/.min/.max on an item without a value",
	       &e[0].items[0]);
	EXPECT(g, "default does not parse with the item's type",
	       &g[0].items[0]);
	EXPECT(h, "default does not parse with the item's type",
	       &h[0].items[0]);
	EXPECT(i, ".min/.max need a numeric type", &i[0].items[0]);
	EXPECT(j, "default does not parse with the item's type",
	       &j[0].items[0]);
}

TEST(structure)
{
	arg_item bad_kind = { .kind = (arg_item_kind)99 };
	const arg_item a[] = { ARG_APP("t", "T", ARG_ARG(NULL, "")) };
	const arg_item b[] = { ARG_APP("t", "T", ARG_CMD(NULL, run, "")) };
	const arg_item c[] = { ARG_APP("t", "T", ARG_CMD("x", NULL, "",
						  ARG_FLAG('f', "f", ""))) };
	const arg_item d[] = { ARG_APP("t", "T", ARG_ALIAS(NULL)) };
	const arg_item e[] = { ARG_APP("t", "T", ARG_MAIN(NULL)) };
	const arg_item g[] = { ARG_APP("t", "T", ARG_MAIN(run),
				       ARG_MAIN(run)) };
	const arg_item h[] = { ARG_APP("t", "T", bad_kind) };
	const arg_item i[] = { ARG_APP("t", "T", ARG_USE(NULL)) };
	const arg_item ok[] = { ARG_APP(
		"t", "T", ARG_CMD("g", NULL, "", ARG_MAIN(run)),
		ARG_CMD("n", ARG_NO_HANDLER, ""), ARG_GROUP("G"),
		ARG_EXAMPLE("t g", "G"), ARG_MUTEX("a", "b"), ARG_ALIAS("x")) };

	CHECK_INT(arg_compile(ok, NULL, NULL), ARG_OK);
	EXPECT(a, "positional without a name", &a[0].items[0]);
	EXPECT(b, "command without a name", &b[0].items[0]);
	EXPECT(c, "command without handler or subcommands", &c[0].items[0]);
	EXPECT(d, "alias without a name", &d[0].items[0]);
	EXPECT(e, "ARG_MAIN without a function", &e[0].items[0]);
	EXPECT(g, "more than one ARG_MAIN", &g[0].items[1]);
	EXPECT(h, "unknown item kind", &h[0].items[0]);
	EXPECT(i, "ARG_USE without a tree", &i[0].items[0]);
}

TEST(duplicates)
{
	const arg_item a[] = { ARG_APP("t", "T", ARG_FLAG('x', "a", ""),
				       ARG_FLAG('x', "b", "")) };
	const arg_item b[] = { ARG_APP("t", "T", ARG_FLAG('x', "a", ""),
				       ARG_FLAG('y', "a", "")) };
	const arg_item c[] = { ARG_APP("t", "T", ARG_FLAG(0, "a", ""),
				       ARG_FLAG(0, "b", "", .alias = "a")) };
	const arg_item d[] = { ARG_APP("t", "T", ARG_FLAG(0, "a", "",
							  .id = "k"),
				       ARG_FLAG(0, "b", "", .id = "k")) };
	const arg_item e[] = { ARG_APP("t", "T", ARG_FLAG(0, "a", ""),
				       ARG_ARG("a", "")) };
	const arg_item g[] = { ARG_APP("t", "T", ARG_CMD("x", run, ""),
				       ARG_CMD("y", run, "", ARG_ALIAS("x"))) };
	const arg_item h[] = { ARG_APP("t", "T", ARG_CMD("x", run, "",
							 ARG_ALIAS("x"))) };
	const arg_item i[] = { ARG_APP("t", "T", ARG_FLAG('x', NULL, ""),
				       ARG_FLAG(0, "y", "", .id = "-x")) };
	const arg_item ok[] = { ARG_APP(
		"t", "T", ARG_FLAG('x', "a", ""),
		ARG_CMD("s", run, "", ARG_FLAG('x', "a", ""))) };

	CHECK_INT(arg_compile(ok, NULL, NULL), ARG_OK);
	EXPECT(a, "duplicate short option", &a[0].items[1]);
	EXPECT(b, "duplicate long option name", &b[0].items[1]);
	EXPECT(c, "duplicate long option name", &c[0].items[1]);
	EXPECT(d, "duplicate lookup key", &d[0].items[1]);
	EXPECT(e, "duplicate lookup key", &e[0].items[1]);
	EXPECT(g, "duplicate command name", &g[0].items[1]);
	EXPECT(h, "duplicate command name", &h[0].items[0]);
	EXPECT(i, "duplicate lookup key", &i[0].items[1]);
}

TEST(positional_order)
{
	const arg_item a[] = { ARG_APP("t", "T", ARG_ARGS("a", ""),
				       ARG_ARG_OPT("b", "")) };
	const arg_item b[] = { ARG_APP("t", "T", ARG_ARG_OPT("a", ""),
				       ARG_ARG("b", "")) };
	const arg_item c[] = { ARG_APP("t", "T", ARG_ARGS("a", "",
							  .max_count = 0),
				       ARG_ARGS("b", "")) };
	const arg_item d[] = { ARG_APP("t", "T",
		{ .kind = ARG_I_ARG, .name = "a", .many = 1, .min_count = 3,
		  .max_count = 2 }) };
	const arg_item ok[] = { ARG_APP("t", "T", ARG_ARG("a", ""),
					ARG_ARG_OPT("b", ""),
					ARG_ARGS_OPT("c", "")) };

	CHECK_INT(arg_compile(ok, NULL, NULL), ARG_OK);
	EXPECT(a, "positional after a variadic one", &a[0].items[1]);
	EXPECT(b, "required positional after an optional one",
	       &b[0].items[1]);
	EXPECT(c, "positional after a variadic one", &c[0].items[1]);
	EXPECT(d, ".max_count below the minimum", &d[0].items[0]);
}

/* Errors deep in the tree: arg_compile sees them, parse only on entry. */
TEST(deep_errors)
{
	const arg_item spec[] = { ARG_APP(
		"t", "T", ARG_FLAG('v', "v", ""),
		ARG_CMD("a", run, "", ARG_CMD("b", run, "",
					      ARG_FLAG(0, NULL, "nameless"))),
		ARG_CMD("ok", run, "")) };
	const arg_item *bad = &spec[0].items[1].items[0];
	arg_error err;
	struct fx f;

	CHECK_INT(arg_compile(spec, NULL, &err), ARG_E_SPEC);
	CHECK(err.cmd == bad);
	CHECK(err.item == &bad->items[0]);
	fx_init(&f, spec);
	CHECK_INT(PARSE(&f, "ok"), ARG_OK);
	CHECK_INT(PARSE(&f, "a"), ARG_OK);
	CHECK_INT(PARSE(&f, "-v", "a", "b"), ARG_E_SPEC);
	CHECK(fx_error(&f)->cmd == bad);
	CHECK_STR(fx_report(&f),
		  "prog a: invalid specification: option without a short or "
		  "long name\n");
	/* help for the broken command finds it as well */
	fx_config(&f, spec);
	f.cfg.style = ARG_HELP_COMMAND;
	fx_start(&f);
	CHECK_INT(PARSE(&f, "a", "help", "b"), ARG_E_SPEC);
	CHECK_INT(PARSE(&f, "a", "help"), ARG_EXIT_HELP);
}

TEST(depth_limit)
{
	arg_item cmds[ARG_MAX_DEPTH + 2][2];
	arg_item root[2];
	arg_error err;
	size_t i;

	memset(cmds, 0, sizeof(cmds));
	memset(root, 0, sizeof(root));
	for (i = 0; i < ARG_MAX_DEPTH + 2; i++) {
		cmds[i][0].kind = ARG_I_CMD;
		cmds[i][0].name = "c";
		cmds[i][0].fn.run = run;
		cmds[i][0].items = i + 1 < ARG_MAX_DEPTH + 2 ? cmds[i + 1]
							     : NULL;
	}
	root[0].kind = ARG_I_CMD;
	root[0].name = "root";
	/* root plus ARG_MAX_DEPTH levels is fine */
	cmds[ARG_MAX_DEPTH - 1][0].items = NULL;
	root[0].items = cmds[0];
	CHECK_INT(arg_compile(root, NULL, &err), ARG_OK);
	cmds[ARG_MAX_DEPTH - 1][0].items = cmds[ARG_MAX_DEPTH];
	CHECK_INT(arg_compile(root, NULL, &err), ARG_E_SPEC);
	CHECK_STR(err.detail, "commands nested deeper than ARG_MAX_DEPTH");
	CHECK(err.item == &cmds[ARG_MAX_DEPTH][0]);
}

TEST(use_limit)
{
	arg_item uses[ARG__USE_MAX + 2][2];
	arg_item root[2];
	arg_error err;
	size_t i;

	memset(uses, 0, sizeof(uses));
	memset(root, 0, sizeof(root));
	for (i = 0; i < ARG__USE_MAX + 1; i++) {
		uses[i][0].kind = ARG_I_USE;
		uses[i][0].items = uses[i + 1];
	}
	uses[ARG__USE_MAX + 1][0].kind = ARG_I_FLAG;
	uses[ARG__USE_MAX + 1][0].name = "deep";
	root[0].kind = ARG_I_CMD;
	root[0].name = "root";
	root[0].items = uses[1];
	CHECK_INT(arg_compile(root, NULL, &err), ARG_OK);
	root[0].items = uses[0];
	CHECK_INT(arg_compile(root, NULL, &err), ARG_E_SPEC);
	CHECK_STR(err.detail, "ARG_USE nested too deeply");
	/* a cycle ends the same way */
	uses[3][0].items = uses[0];
	CHECK_INT(arg_compile(root, NULL, &err), ARG_E_SPEC);
}

TEST(too_many_items)
{
	size_t n = (size_t)UINT16_MAX + 2, i;
	arg_item *kids = calloc(n + 1, sizeof(*kids));
	arg_item root[2];
	arg_error err;

	REQUIRE(kids != NULL);
	for (i = 0; i < n; i++) {
		kids[i].kind = ARG_I_ARG;
		kids[i].name = "p";
	}
	memset(root, 0, sizeof(root));
	root[0].kind = ARG_I_CMD;
	root[0].name = "root";
	root[0].items = kids;
	CHECK_INT(arg_compile(root, NULL, &err), ARG_E_SPEC);
	CHECK_STR(err.detail, "too many items in a command");
	free(kids);
}

TEST(many_shorts)
{
	/* more than 254 options: the short table falls back to a scan */
	arg_item *kids = calloc(302, sizeof(*kids));
	char (*names)[8] = calloc(300, sizeof(*names));
	arg_item root[2];
	struct fx f;
	size_t i;

	REQUIRE(kids != NULL && names != NULL);
	for (i = 0; i < 300; i++) {
		snprintf(names[i], sizeof(names[i]), "o%zu", i);
		kids[i].kind = ARG_I_FLAG;
		kids[i].name = names[i];
	}
	kids[260].short_name = 'z';
	kids[100].short_name = 'a';
	kids[299].short_name = 'q';
	memset(root, 0, sizeof(root));
	root[0].kind = ARG_I_CMD;
	root[0].name = "root";
	root[0].items = kids;
	fx_config(&f, root);
	f.cfg.alloc = arg_alloc_stdlib;
	fx_start(&f);
	REQUIRE(PARSE(&f, "-za", "--o299", "--o0") == ARG_OK);
	CHECK(arg_flag(&f.p, "o260"));
	CHECK(arg_flag(&f.p, "o100"));
	CHECK(arg_flag(&f.p, "o299"));
	CHECK(arg_flag(&f.p, "o0"));
	CHECK(!arg_flag(&f.p, "o1"));
	CHECK_INT(PARSE(&f, "-y"), ARG_E_UNKNOWN_OPTION);
	kids[299].short_name = 'z';
	CHECK_INT(PARSE0(&f), ARG_E_SPEC);
	CHECK_STR(fx_error(&f)->detail, "duplicate short option");
	arg_free(&f.p);
	free(kids);
	free(names);
}

TEST(compile_config)
{
	const arg_item spec[] = { ARG_APP("t", "T", ARG_FLAG('v', "v", "")) };
	unsigned char small[16];
	arg_config cfg = ARG_CONFIG_INIT;
	arg_error err;

	CHECK_INT(arg_compile(spec, &cfg, &err), ARG_OK);
	CHECK_INT(err.status, ARG_OK);
	/* a caller arena that is too small and no alloc hook */
	cfg.arena = small;
	cfg.arena_cap = sizeof(small);
	cfg.alloc = NULL;
	CHECK_INT(arg_compile(spec, &cfg, &err), ARG_OK);
}

struct walk {
	unsigned n, cmds;
	unsigned max_depth;
	const arg_item *stop_at;
	const arg_item *parent_of_add;
};

static int visit(void *ud, const arg_item *it, const arg_item *parent,
		 unsigned depth)
{
	struct walk *w = ud;

	w->n++;
	if (it->kind == ARG_I_CMD)
		w->cmds++;
	if (depth > w->max_depth)
		w->max_depth = depth;
	if (it->name && strcmp(it->name, "add") == 0)
		w->parent_of_add = parent;
	return it == w->stop_at ? 17 : 0;
}

TEST(foreach)
{
	const arg_item tree[] = {
		ARG_CMD("repo", NULL, "R", ARG_FLAG('q', "q", ""),
			ARG_CMD("add", run, "A", ARG_ARG("n", ""))),
		ARG__END
	};
	const arg_item spec[] = { ARG_APP("t", "T", ARG_FLAG('v', "v", ""),
					  ARG_USE(tree), ARG_GROUP("G")) };
	struct walk w;

	memset(&w, 0, sizeof(w));
	CHECK_INT(arg_foreach_item(spec, visit, &w), 0);
	CHECK_UINT(w.n, 7);
	CHECK_UINT(w.cmds, 3);
	CHECK_UINT(w.max_depth, 3);
	CHECK(w.parent_of_add == &tree[0]);
	memset(&w, 0, sizeof(w));
	w.stop_at = &tree[0].items[0];
	CHECK_INT(arg_foreach_item(spec, visit, &w), 17);
	CHECK_UINT(w.n, 4);
	CHECK_INT(arg_foreach_item(NULL, visit, &w), 0);
	CHECK_INT(arg_foreach_item(spec, NULL, &w), 0);
}

TEST_MAIN(TEST_CASE(root), TEST_CASE(option_names),
	  TEST_CASE(functions_and_types), TEST_CASE(bindings),
	  TEST_CASE(texts), TEST_CASE(structure), TEST_CASE(duplicates),
	  TEST_CASE(positional_order), TEST_CASE(deep_errors),
	  TEST_CASE(depth_limit), TEST_CASE(use_limit),
	  TEST_CASE(too_many_items), TEST_CASE(many_shorts),
	  TEST_CASE(compile_config), TEST_CASE(foreach))
