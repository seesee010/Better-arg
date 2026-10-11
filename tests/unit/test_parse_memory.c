// SPDX-License-Identifier: GPL-2.0-only
/*
 * test_parse_memory.c - arena tiers, exhaustion, the alloc hook and
 * arg_free() (SDD 13)
 */
#include "parse_util.h"

#include <stdlib.h>

/* A counting allocator; fails once `budget` blocks are out. */
struct pool {
	unsigned live, total;
	unsigned budget;
	size_t bytes;
};

static void *pool_alloc(void *ud, void *ptr, size_t old, size_t size)
{
	struct pool *p = ud;

	if (size == 0) {
		p->live--;
		p->bytes -= old;
		free(ptr);
		return NULL;
	}
	if (ptr || (p->budget && p->total >= p->budget))
		return NULL;
	ptr = malloc(size);
	if (ptr) {
		p->live++;
		p->total++;
		p->bytes += size;
	}
	return ptr;
}

static const arg_item app[] = { ARG_APP(
	"t", "T", ARG_INT('n', "num", "Numbers", .many = 1),
	ARG_STR('t', "tags", "Tags", .many = 1, .sep = ','),
	ARG_DOUBLE('d', "ratio", "Ratio", .def = "0.25"),
	ARG_ARGS_OPT("files", "Files")) };

/* argv with n copies of "-n<i>" */
static char **many_args(size_t n, char *store, size_t width)
{
	char **argv = calloc(n + 2, sizeof(*argv));
	size_t i;

	argv[0] = "prog";
	for (i = 0; i < n; i++) {
		snprintf(store + i * width, width, "-n%zu", i);
		argv[i + 1] = store + i * width;
	}
	return argv;
}

TEST(inline_arena_is_enough)
{
	struct fx f;

	fx_init(&f, app);
	REQUIRE(PARSE(&f, "-n1", "-n2", "--tags=a,b,c", "x", "y") == ARG_OK);
	CHECK_UINT(arg_count(&f.p, "tags"), 3);
	CHECK_DBL(arg_double(&f.p, "ratio"), 0.25, 0);
	CHECK_STR(arg_path(&f.p), "prog");
	arg_free(&f.p);
	arg_free(&f.p);
}

TEST(tiny_caller_arena)
{
	unsigned char small[64];
	struct fx f;

	fx_config(&f, app);
	f.cfg.arena = small;
	f.cfg.arena_cap = sizeof(small);
	fx_start(&f);
	CHECK_INT(PARSE0(&f), ARG_E_NO_MEMORY);
	CHECK_STR(fx_report(&f), "prog: out of memory\n");
	CHECK_INT(arg_report(&f.p), 70);
	CHECK_INT(arg_run(&f.p), 70);
}

TEST(exhaustion_while_parsing)
{
	char store[2000 * 8], commas[200];
	char **argv = many_args(2000, store, 8);
	struct fx f;

	REQUIRE(argv != NULL);
	memset(commas, ',', sizeof(commas) - 1);
	commas[sizeof(commas) - 1] = '\0';
	fx_init(&f, app);
	CHECK_INT(arg_parse(&f.p, 2001, argv), ARG_E_NO_MEMORY);
	/* splitting copies need memory, too */
	CHECK_INT(PARSE(&f, "--tags",
			commas),
		  ARG_E_NO_MEMORY);
	free(argv);
}

TEST(growth_with_alloc_hook)
{
	char store[3000 * 8];
	char **argv = many_args(3000, store, 8);
	struct pool pool = { 0, 0, 0, 0 };
	struct fx f;
	size_t i;

	REQUIRE(argv != NULL);
	fx_config(&f, app);
	f.cfg.alloc = pool_alloc;
	f.cfg.alloc_ud = &pool;
	fx_start(&f);
	REQUIRE(arg_parse(&f.p, 3001, argv) == ARG_OK);
	CHECK_UINT(arg_count(&f.p, "num"), 3000);
	for (i = 0; i < 3000; i += 299)
		CHECK_INT(arg_int_at(&f.p, "num", i), (int64_t)i);
	CHECK(pool.live > 0);
	/* blocks grow, so there are few of them */
	CHECK(pool.live < 10);
	arg_free(&f.p);
	CHECK_UINT(pool.live, 0);
	CHECK_UINT(pool.bytes, 0);
	arg_free(&f.p);
	CHECK_UINT(pool.live, 0);
	/* a freed parser can parse again */
	REQUIRE(arg_parse(&f.p, 3001, argv) == ARG_OK);
	/* a new parse returns the old blocks first */
	REQUIRE(PARSE0(&f) == ARG_OK);
	CHECK_UINT(pool.live, 0);
	arg_free(&f.p);
	free(argv);
}

TEST(stdlib_allocator)
{
	char store[500 * 8];
	char **argv = many_args(500, store, 8);
	struct fx f;

	REQUIRE(argv != NULL);
	fx_config(&f, app);
	f.cfg.alloc = arg_alloc_stdlib;
	fx_start(&f);
	REQUIRE(arg_parse(&f.p, 501, argv) == ARG_OK);
	CHECK_INT(arg_int_at(&f.p, "num", 499), 499);
	arg_free(&f.p); /* ASan reports a leak otherwise */
	free(argv);
}

TEST(alloc_hook_failure)
{
	char store[2000 * 8];
	char **argv = many_args(2000, store, 8);
	struct pool pool = { 0, 0, 1, 0 };
	struct fx f;

	REQUIRE(argv != NULL);
	fx_config(&f, app);
	f.cfg.alloc = pool_alloc;
	f.cfg.alloc_ud = &pool;
	fx_start(&f);
	CHECK_INT(arg_parse(&f.p, 2001, argv), ARG_E_NO_MEMORY);
	CHECK_UINT(pool.total, 1);
	arg_free(&f.p);
	CHECK_UINT(pool.live, 0);
	free(argv);
}

TEST(alignment)
{
	union {
		double d;
		unsigned char b[512];
	} buf;
	struct fx f;
	size_t i;

	fx_config(&f, app);
	f.cfg.arena = buf.b + 1; /* deliberately misaligned */
	f.cfg.arena_cap = sizeof(buf.b) - 1;
	fx_start(&f);
	for (i = 1; i < 9; i++) {
		void *p = arg__alloc(&f.p, i);

		REQUIRE(p != NULL);
		CHECK_UINT((uintptr_t)p % sizeof(double), 0);
	}
	REQUIRE(PARSE(&f, "-d", "1.5") == ARG_OK);
	CHECK_DBL(arg_double(&f.p, "ratio"), 1.5, 0);
	CHECK(arg__alloc(&f.p, sizeof(buf)) == NULL);
	CHECK(arg__alloc(&f.p, 0) != NULL);
}

TEST(path_without_memory)
{
	struct fx f;

	fx_init(&f, app);
	REQUIRE(PARSE0(&f) == ARG_OK);
	while (arg__alloc(&f.p, 8))
		;
	CHECK(arg_path(&f.p) == NULL);
	REQUIRE(PARSE0(&f) == ARG_OK);
	CHECK_STR(arg_path(&f.p), "prog");
	while (arg__alloc(&f.p, 8))
		;
	CHECK_STR(arg_path(&f.p), "prog"); /* cached */
}

TEST(rest_without_memory)
{
	unsigned char small[700];
	char *argv[202];
	struct fx f;
	size_t i;

	argv[0] = "prog";
	for (i = 1; i < 201; i++)
		argv[i] = i % 2 ? "--unknown" : "-vz";
	argv[201] = NULL;
	fx_config(&f, app);
	f.cfg.arena = small;
	f.cfg.arena_cap = sizeof(small);
	f.cfg.style = ARG_ALLOW_UNKNOWN;
	fx_start(&f);
	REQUIRE(PARSE(&f, "--a", "-vz", "-z") == ARG_OK);
	CHECK_UINT(arg_rest_count(&f.p), 3);
	CHECK_INT(fx_parse(&f, argv), ARG_E_NO_MEMORY);
}

TEST_MAIN(TEST_CASE(inline_arena_is_enough), TEST_CASE(tiny_caller_arena),
	  TEST_CASE(exhaustion_while_parsing),
	  TEST_CASE(growth_with_alloc_hook), TEST_CASE(stdlib_allocator),
	  TEST_CASE(alloc_hook_failure), TEST_CASE(alignment),
	  TEST_CASE(path_without_memory), TEST_CASE(rest_without_memory))
