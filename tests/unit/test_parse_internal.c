// SPDX-License-Identifier: GPL-2.0-only
/*
 * test_parse_internal.c - the private interface used by rules.c,
 * help.c and complete.c (arg_internal.h, parse.c section), and a
 * sweep over every arena size to prove exhaustion is always clean.
 */
#include "parse_util.h"

static int run(arg *a)
{
	(void)a;
	return 0;
}

ARG_DEFINE(extra, ARG_FLAG('x', "extra", "Extra"), ARG_GROUP("More"),
	   ARG_EXAMPLE("t -x", "Extra"));

static const arg_item app[] = { ARG_APP(
	"t", "T", ARG_FLAG('v', "verbose", "V", .global = 1),
	ARG_STR(0, "out", "Out", .env = "T_OUT"),
	ARG_FLAG(0, "color", "C", .negatable = 1),
	ARG_USE(extra), ARG_MUTEX("verbose", "out"),
	ARG_ARG_OPT("file", "File"),
	ARG_CMD("repo", NULL, "R", ARG_ALIAS("r"),
		ARG_CMD("add", run, "A", ARG_ALIAS("a"), ARG_ARG("n", "N")),
		ARG_CMD("list", run, "L")),
	ARG_MAIN(run)) };

TEST(iterator)
{
	struct arg__iter it, copy;
	const arg_item *x;
	unsigned n = 0, groups = 0;

	arg__iter_init(&it, &app[0]);
	while ((x = arg__iter_next(&it))) {
		CHECK(x->kind != ARG_I_USE && x->kind != ARG_I_END);
		groups += x->kind == ARG_I_GROUP;
		if (n++ == 2) {
			copy = it;
			CHECK(arg__iter_next(&copy) == &extra[0]);
		}
	}
	CHECK_UINT(n, 10);
	CHECK_UINT(groups, 1);
	CHECK(it.error == NULL);
	arg__iter_init(&it, NULL);
	CHECK(arg__iter_next(&it) == NULL);
}

TEST(index_and_lookups)
{
	struct arg__state *s;
	struct arg__cmd *c;
	struct fx f;
	bool neg = false;

	fx_init(&f, app);
	REQUIRE(PARSE(&f, "-v", "--no-color", "r", "a", "x") == ARG_OK);
	CHECK_UINT(arg__priv(&f.p)->depth, 3);
	c = arg__priv(&f.p)->idx[0];
	CHECK(c->cmd == &app[0]);
	CHECK(c->parent == NULL);
	CHECK_UINT(c->nopt, 4);
	CHECK_UINT(c->npos, 1);
	CHECK_UINT(c->nsub, 1);
	CHECK(c->main == &app[0].items[7]);
	CHECK(c->st[3].item == &extra[0]);
	CHECK(arg__priv(&f.p)->idx[2]->parent == arg__priv(&f.p)->idx[1]);
	CHECK_UINT(arg__priv(&f.p)->idx[2]->level, 2);

	s = arg__find_option(c, "verbose");
	REQUIRE(s != NULL);
	CHECK(s->item == &app[0].items[0]);
	CHECK_INT(s->source, ARG_SRC_CLI);
	CHECK_UINT(s->count, 1);
	CHECK(arg__find_option(c, "-v") == s);
	CHECK(arg__find_option(c, "-x")->item == &extra[0]);
	CHECK(arg__find_option(c, "no-color") == NULL);
	CHECK(arg__find_option(c, "") == NULL);
	CHECK(arg__find_option(c, NULL) == NULL);
	CHECK(arg__find_option(c, "--verbose") == NULL);
	CHECK(arg__long_lookup(c, "no-color", 8, &neg) != NULL);
	CHECK(neg);
	CHECK(arg__long_lookup(c, "colorx", 5, NULL)->item ==
	      &app[0].items[2]);
	CHECK(arg__short_lookup(c, 'q') == NULL);
	CHECK(arg__short_lookup(c, (char)0xe9) == NULL);
	/* commands without short options have no short table */
	CHECK(arg__priv(&f.p)->idx[1]->shorts == NULL);
	CHECK(arg__short_lookup(arg__priv(&f.p)->idx[1], 'v') == NULL);

	CHECK(arg__state_of(&f.p, &app[0].items[0]) == s);
	CHECK(arg__state_of(&f.p, &app[0].items[7]) == NULL);
	CHECK(arg__state_is_set(&f.p, s));
	CHECK(!arg__state_is_set(&f.p, arg__find_option(c, "out")));
	CHECK_UINT(arg__state_len(s), 1);
	CHECK(arg__state_value(s, 1) == NULL);
}

TEST(item_helpers)
{
	const arg_item cnt = ARG_COUNT('c', NULL, "C");
	const arg_item on = ARG_ON('o', "on", NULL, "O");
	const arg_item pos = ARG_ARG("p", "P");
	const arg_item id = ARG_FLAG(0, "x", "X", .id = "key");
	const arg_item grp = ARG_GROUP("G");
	char kb[3];

	CHECK(arg__value_type(&cnt) == &arg_t_u32);
	CHECK(arg__value_type(&on) == NULL);
	CHECK(arg__value_type(&pos) == &arg_t_string);
	CHECK(arg__value_type(&grp) == NULL);
	CHECK(arg__value_type(&id) == &arg_t_bool);
	CHECK_STR(arg__item_key(&cnt, kb), "-c");
	CHECK_STR(arg__item_key(&on, kb), "on");
	CHECK_STR(arg__item_key(&id, kb), "key");
	CHECK_STR(arg__item_key(&pos, kb), "p");
}

TEST(subcommand_lookup)
{
	CHECK(arg__sub_lookup(&app[0], "repo", 4) == &app[0].items[6]);
	CHECK(arg__sub_lookup(&app[0], "r", 1) == &app[0].items[6]);
	CHECK(arg__sub_lookup(&app[0], "repox", 4) == &app[0].items[6]);
	CHECK(arg__sub_lookup(&app[0], "re", 2) == NULL);
	CHECK(arg__sub_lookup(&app[0].items[6], "a", 1) ==
	      &app[0].items[6].items[1]);
	CHECK(arg__sub_lookup(&app[0].items[6], "n", 1) == NULL);
}

TEST(find_cmd)
{
	struct arg__pathbuf pb;
	struct fx f;

	fx_init(&f, app);
	/* before a parse the selection is the root */
	CHECK(arg__find_cmd(&f.p, NULL, &pb) == &app[0]);
	CHECK_UINT(pb.path.depth, 1);
	CHECK(arg__find_cmd(&f.p, "r add", &pb) == &app[0].items[6].items[1]);
	CHECK_UINT(pb.path.depth, 3);
	CHECK(pb.path.cmds == pb.cmds);
	CHECK(arg__find_cmd(&f.p, "  ", &pb) == &app[0]);
	CHECK(arg__find_cmd(&f.p, "repo x", &pb) == NULL);
	REQUIRE(PARSE(&f, "repo", "list") == ARG_OK);
	CHECK(arg__find_cmd(&f.p, NULL, &pb) == &app[0].items[6].items[2]);
	CHECK_STR(pb.path.prog, "prog");

	fx_init(&f, NULL);
	CHECK(arg__find_cmd(&f.p, "", &pb) == NULL);
	CHECK(!arg__cmd_path(&f.p, &app[0], &pb));
}

TEST(cmd_path)
{
	struct arg__pathbuf pb;
	struct fx f;
	const arg_item lone = ARG_CMD("lone", run, "Not in the tree");

	fx_init(&f, app);
	CHECK(arg__cmd_path(&f.p, &app[0].items[6].items[1], &pb));
	CHECK_UINT(pb.path.depth, 3);
	CHECK(pb.cmds[1] == &app[0].items[6]);
	CHECK(!arg__cmd_path(&f.p, &lone, &pb));
	REQUIRE(PARSE(&f, "repo", "add", "x") == ARG_OK);
	CHECK(arg__cmd_path(&f.p, &app[0].items[6], &pb));
	CHECK_UINT(pb.path.depth, 2);
	CHECK(arg__cmd_path(&f.p, &app[0].items[6].items[2], &pb));
	CHECK_UINT(pb.path.depth, 3);
}

TEST(cmd_path_depth)
{
	arg_item cmds[ARG_MAX_DEPTH + 3][2];
	char path[2 * (ARG_MAX_DEPTH + 2)];
	struct arg__pathbuf pb;
	struct fx f;
	size_t i;

	memset(cmds, 0, sizeof(cmds));
	for (i = 0; i < ARG_MAX_DEPTH + 3; i++) {
		cmds[i][0].kind = ARG_I_CMD;
		cmds[i][0].name = "c";
		cmds[i][0].fn.run = run;
		cmds[i][0].items = i + 1 < ARG_MAX_DEPTH + 3 ? cmds[i + 1]
							     : NULL;
	}
	fx_init(&f, cmds[0]);
	CHECK(arg__cmd_path(&f.p, &cmds[ARG_MAX_DEPTH][0], &pb));
	CHECK_UINT(pb.path.depth, ARG_MAX_DEPTH + 1);
	CHECK(!arg__cmd_path(&f.p, &cmds[ARG_MAX_DEPTH + 1][0], &pb));
	/* paths longer than the depth limit are never found */
	path[0] = '\0';
	for (i = 0; i < ARG_MAX_DEPTH; i++)
		strcat(path, "c ");
	CHECK(arg__find_cmd(&f.p, path, &pb) == &cmds[ARG_MAX_DEPTH][0]);
	strcat(path, "c");
	CHECK(arg__find_cmd(&f.p, path, &pb) == NULL);
}

TEST(errors_and_fail)
{
	struct arg__path path;
	struct fx f;

	fx_init(&f, app);
	arg__selected_path(&f.p, &path);
	CHECK_UINT(path.depth, 1);
	CHECK(path.cmds[0] == &app[0]);
	CHECK(path.prog == NULL);
	REQUIRE(PARSE(&f, "repo", "add", "x") == ARG_OK);
	CHECK_INT(arg__set_error(&f.p, ARG_E_RULE, &app[0].items[0],
				 "too much"),
		  ARG_E_RULE);
	CHECK(fx_error(&f)->cmd == &app[0].items[6].items[1]);
	CHECK_INT(fx_error(&f)->argi, -1);
	CHECK(arg__take_fail(&f.p) == NULL);
	arg_fail(&f.p, "msg");
	CHECK_STR(arg__take_fail(&f.p), "msg");
	CHECK(arg__take_fail(&f.p) == NULL);
	arg__selected_path(&f.p, &path);
	CHECK_UINT(path.depth, 3);
	CHECK_STR(path.prog, "prog");
}

TEST(default_config)
{
	arg p;
	char *argv[] = { "t", "--out", "x", NULL };
	const struct arg__priv *v;

	arg_init(&p, app, NULL);
	v = arg__priv(&p);
	CHECK_UINT(v->cfg.version, ARG_CONFIG_VERSION);
	CHECK(v->cfg.write_out == arg__write_stdout);
	CHECK(v->cfg.write_err == arg__write_stderr);
	CHECK(v->cfg.getenv == arg__getenv_default);
	CHECK(v->cfg.exit == arg__exit_default);
	CHECK_UINT(v->cfg.rule_sources, ARG_RULE_SOURCES_DEFAULT);
	CHECK_INT(v->exit_on_error, 0);
	CHECK(v->arena.buf == p.arena_inline.bytes);
	CHECK_INT(arg_parse(&p, 3, argv), ARG_OK);
	CHECK_STR(arg_str(&p, "out"), "x");
	arg_free(&p);
}

TEST(nameless_root)
{
	const arg_item spec[] = { { .kind = ARG_I_CMD,
				    .items = (const arg_item[]){
					    ARG_FLAG('v', "v", ""),
					    ARG__END } } };
	struct fx f;

	fx_init(&f, spec);
	REQUIRE(arg_parse(&f.p, 0, NULL) == ARG_OK);
	CHECK_STR(arg_path(&f.p), "");
	CHECK_INT(arg_run(&f.p), 0); /* no handler at all */
}

/* Odd custom types: an INT kind whose size the loaders do not know. */
static int odd_parse(const arg_type *t, const char *s, size_t n, void *dst,
		     arg_ctx *ctx)
{
	(void)t;
	(void)s;
	(void)n;
	(void)ctx;
	memset(dst, 1, 3);
	return 0;
}

static const arg_type t_odd_int = { .name = "ODD", .size = 3,
				    .parse = odd_parse, .kind = ARG_K_INT };
static const arg_type t_odd_uint = { .name = "ODD", .size = 3,
				     .parse = odd_parse, .kind = ARG_K_UINT };

TEST(odd_sizes)
{
	int bound = 77;
	const arg_item spec[] = { ARG_APP(
		"t", "T",
		ARG_CUSTOM(0, "i", &t_odd_int, "", .to = ARG_TO_INT(&bound)),
		ARG_CUSTOM(0, "u", &t_odd_uint, "")) };
	struct fx f;

	fx_init(&f, spec);
	REQUIRE(PARSE(&f, "--i=1", "--u=1") == ARG_OK);
	CHECK_INT(bound, 77);
	CHECK_INT(arg_int(&f.p, "i"), 0);
	CHECK_UINT(arg_uint(&f.p, "u"), 0);
}

/* ------------------------------------------------------------------ */
/* Exhaustion sweep                                                   */
/* ------------------------------------------------------------------ */

static int on_x(arg *a, const char *v)
{
	(void)a;
	(void)v;
	return 0;
}

static int ok_check(arg *a, const char *key)
{
	(void)a;
	(void)key;
	return 0;
}

static const arg_item rich[] = { ARG_APP(
	"rich", "Every feature at once",
	ARG_COUNT('v', "verbose", "V", .global = 1, .max = "9"),
	ARG_INT('j', "jobs", "J", .def = "4", .min = "1", .max = "64",
		.check = ok_check),
	ARG_STR('t', "tags", "T", .many = 1, .sep = ','),
	ARG_FLAG(0, "color", "C", .negatable = 1, .alias = "colour"),
	ARG_ON('D', "define", on_x, "D", .value = "K=V"),
	ARG_STR(0, "opt", "O", .optional_value = 1, .def = "dflt"),
	ARG_CMD("build", run, "B", ARG_ALIAS("b"),
		ARG_DOUBLE('r', "ratio", "R", .min = "0", .def = "0.5"),
		ARG_ARGS("files", "F", .max_count = 9)),
	ARG_MAIN(run)) };

TEST(exhaustion_sweep)
{
	char *argv[] = { "rich", "-vv", "--jobs=8", "-ta,b,c", "--no-color",
			 "-DK=V", "--opt", "build", "-r", "2", "x", "y", "-v",
			 NULL };
	unsigned char buf[4096];
	unsigned ok = 0, oom = 0;
	struct fx f;
	size_t cap;

	for (cap = 0; cap <= sizeof(buf); cap += 4) {
		arg_status st;

		fx_config(&f, rich);
		f.cfg.arena = buf;
		f.cfg.arena_cap = cap;
		if (cap == 0)
			f.cfg.arena = NULL; /* the inline arena */
		fx_start(&f);
		st = fx_parse(&f, argv);
		if (st == ARG_E_NO_MEMORY) {
			oom++;
			continue;
		}
		CHECK_INT(st, ARG_OK);
		CHECK_UINT(arg_count(&f.p, "files"), 2);
		CHECK_INT(arg_int(&f.p, "verbose"), 3);
		ok++;
	}
	CHECK(ok > 0);
	CHECK(oom > 0);
	CHECK_INT(arg_compile(rich, NULL, NULL), ARG_OK);
}

TEST_MAIN(TEST_CASE(iterator), TEST_CASE(index_and_lookups),
	  TEST_CASE(item_helpers), TEST_CASE(subcommand_lookup),
	  TEST_CASE(find_cmd), TEST_CASE(cmd_path), TEST_CASE(cmd_path_depth),
	  TEST_CASE(errors_and_fail), TEST_CASE(default_config),
	  TEST_CASE(nameless_root), TEST_CASE(odd_sizes),
	  TEST_CASE(exhaustion_sweep))
