// SPDX-License-Identifier: GPL-2.0-only
/*
 * test_suggest.c - "did you mean" engine: distance and suggester
 */
#include "arg_internal.h"
#include "test.h"

#include <limits.h>

static unsigned dist(const char *a, const char *b, unsigned max)
{
	return arg__distance(a, strlen(a), b, strlen(b), max);
}

/* Unbounded reference OSA distance, full matrix, for cross-checks. */
static unsigned ref_osa(const char *a, const char *b)
{
	unsigned d[ARG__SUGGEST_MAX_LEN + 1][ARG__SUGGEST_MAX_LEN + 1];
	size_t an = strlen(a), bn = strlen(b), i, j;

	for (i = 0; i <= an; i++)
		d[i][0] = (unsigned)i;
	for (j = 0; j <= bn; j++)
		d[0][j] = (unsigned)j;
	for (i = 1; i <= an; i++) {
		for (j = 1; j <= bn; j++) {
			unsigned cost = a[i - 1] == b[j - 1] ? 0u : 1u;
			unsigned v = d[i - 1][j] + 1;

			if (d[i][j - 1] + 1 < v)
				v = d[i][j - 1] + 1;
			if (d[i - 1][j - 1] + cost < v)
				v = d[i - 1][j - 1] + cost;
			if (i > 1 && j > 1 && a[i - 1] == b[j - 2] &&
			    a[i - 2] == b[j - 1] && d[i - 2][j - 2] + 1 < v)
				v = d[i - 2][j - 2] + 1;
			d[i][j] = v;
		}
	}
	return d[an][bn];
}

struct dist_case {
	const char *a;
	const char *b;
	unsigned want;
};

static const struct dist_case dist_table[] = {
	{ "", "", 0 },
	{ "", "a", 1 },
	{ "abc", "", 3 },
	{ "same", "same", 0 },
	{ "kitten", "sitting", 3 },
	{ "sitting", "kitten", 3 },
	{ "flaw", "lawn", 2 },
	{ "ab", "ba", 1 },
	{ "abcd", "acbd", 1 },
	{ "ca", "abc", 3 },
	{ "a", "b", 1 },
	{ "jbos", "jobs", 1 },
	{ "verbsoe", "verbose", 1 },
	{ "instal", "install", 1 },
	{ "verb", "verbose", 3 },
	{ "Verbose", "verbose", 1 },
	{ "help", "hlep", 1 },
	{ "abcdef", "badcfe", 3 },
	{ "aaxx", "xxaa", 3 },
	{ "xyz", "verbose", 7 },
	{ "colour", "color", 1 },
	{ "\xff\xfe", "\xfe\xff", 1 },
};

TEST(distance_table)
{
	size_t i;

	for (i = 0; i < ARG__COUNTOF(dist_table); i++) {
		const struct dist_case *c = &dist_table[i];
		unsigned got = dist(c->a, c->b, 64);

		t_->checks++;
		if (got != c->want)
			TEST_FAIL_("d(\"%s\", \"%s\") = %u, want %u", c->a,
				   c->b, got, c->want);
		CHECK_UINT(ref_osa(c->a, c->b), c->want);
	}
}

TEST(distance_symmetric)
{
	size_t i, j;

	for (i = 0; i < ARG__COUNTOF(dist_table); i++) {
		const struct dist_case *c = &dist_table[i];

		for (j = 0; j <= 8; j++)
			CHECK_UINT(dist(c->a, c->b, (unsigned)j),
				   dist(c->b, c->a, (unsigned)j));
	}
}

TEST(distance_exact_bound_is_kept)
{
	CHECK_UINT(dist("kitten", "sitting", 3), 3);
	CHECK_UINT(dist("ab", "ba", 1), 1);
	CHECK_UINT(dist("same", "same", 0), 0);
	CHECK_UINT(dist("", "abc", 3), 3);
}

TEST(distance_bounded_early_exit)
{
	/* Length difference alone exceeds the bound. */
	CHECK_UINT(dist("a", "abcdef", 2), 3);
	CHECK_UINT(dist("", "abc", 2), 3);
	CHECK_UINT(dist("xyz", "verbose", 1), 2);
	/* Same length, every row runs past max. */
	CHECK_UINT(dist("abcdef", "uvwxyz", 2), 3);
	CHECK_UINT(dist("a", "b", 0), 1);
	CHECK_UINT(dist("ab", "ba", 0), 1);
	/* Rows stay within max but the corner does not. */
	CHECK_UINT(dist("aaxx", "xxaa", 2), 3);
	CHECK_UINT(dist("aaxx", "xxaa", 3), 3);
	CHECK_UINT(dist("kitten", "sitting", 2), 3);
}

TEST(distance_max_saturates)
{
	char long_s[ARG__SUGGEST_MAX_LEN + 2];

	memset(long_s, 'a', sizeof(long_s) - 1);
	long_s[sizeof(long_s) - 1] = '\0';
	CHECK_UINT(dist("kitten", "sitting", UINT_MAX), 3);
	CHECK_UINT(dist(long_s, "a", UINT_MAX), UINT_MAX);
	CHECK_UINT(dist("", "", UINT_MAX), 0);
}

TEST(distance_length_limit)
{
	char s64[ARG__SUGGEST_MAX_LEN + 1];
	char t64[ARG__SUGGEST_MAX_LEN + 1];
	char s65[ARG__SUGGEST_MAX_LEN + 2];

	memset(s64, 'a', ARG__SUGGEST_MAX_LEN);
	s64[ARG__SUGGEST_MAX_LEN] = '\0';
	memcpy(t64, s64, sizeof(t64));
	t64[10] = 'b';
	memset(s65, 'a', ARG__SUGGEST_MAX_LEN + 1);
	s65[ARG__SUGGEST_MAX_LEN + 1] = '\0';

	/* 64 bytes is still compared... */
	CHECK_UINT(dist(s64, s64, 3), 0);
	CHECK_UINT(dist(s64, t64, 3), 1);
	/* ...65 is refused on either side, even when identical. */
	CHECK_UINT(dist(s65, s65, 3), 4);
	CHECK_UINT(dist(s65, s64, 3), 4);
	CHECK_UINT(dist(s64, s65, 3), 4);
	CHECK_UINT(dist(s65, "", 0), 1);
	CHECK_UINT(dist("", s65, 70), 71);
}

TEST(distance_not_nul_terminated)
{
	/* Only an/bn bytes are looked at. */
	CHECK_UINT(arg__distance("jobsXXX", 4, "jobs", 4, 3), 0);
	CHECK_UINT(arg__distance("verbose=1", 7, "verbsoe", 7, 3), 1);
	CHECK_UINT(arg__distance(NULL, 0, "abc", 3, 3), 3);
	CHECK_UINT(arg__distance("abc", 3, NULL, 0, 3), 3);
}

/* Cross-check against the reference over many generated pairs. */
TEST(distance_matches_reference)
{
	static const char alpha[] = "abc";
	unsigned seed = 12345;
	int round;

	for (round = 0; round < 3000; round++) {
		char a[12], b[12];
		size_t an, bn, k;
		unsigned max, want, got;

		seed = seed * 1103515245u + 12345u;
		an = (seed >> 16) % 11;
		seed = seed * 1103515245u + 12345u;
		bn = (seed >> 16) % 11;
		for (k = 0; k < an; k++) {
			seed = seed * 1103515245u + 12345u;
			a[k] = alpha[(seed >> 16) % 3];
		}
		a[an] = '\0';
		for (k = 0; k < bn; k++) {
			seed = seed * 1103515245u + 12345u;
			b[k] = alpha[(seed >> 16) % 3];
		}
		b[bn] = '\0';
		seed = seed * 1103515245u + 12345u;
		max = (seed >> 16) % 7;

		want = ref_osa(a, b);
		got = dist(a, b, max);
		t_->checks++;
		if (want <= max ? got != want : got <= max)
			TEST_FAIL_("d(\"%s\", \"%s\", %u) = %u, ref %u", a,
				   b, max, got, want);
		CHECK_UINT(dist(a, b, 64), want);
	}
}

/* ------------------------------------------------------------------ */
/* Suggester                                                          */
/* ------------------------------------------------------------------ */

static const char *suggest(const char *input, const char *const *cands)
{
	struct arg__suggester s;

	arg__suggest_init(&s, input, strlen(input), NULL);
	for (; *cands; cands++)
		arg__suggest_add(&s, *cands);
	return arg__suggest_best(&s);
}

static const char *const opts[] = {
	"jobs", "verbose", "version", "install", "help", "output", NULL
};

TEST(suggest_init_limits)
{
	static const struct {
		const char *in;
		unsigned limit;
	} t[] = {
		{ "", 1 },      { "a", 1 },       { "abc", 1 },
		{ "abcd", 2 },  { "abcdef", 2 },  { "abcdefg", 3 },
		{ "abcdefghijklmnop", 3 },
	};
	size_t i;

	for (i = 0; i < ARG__COUNTOF(t); i++) {
		struct arg__suggester s;

		arg__suggest_init(&s, t[i].in, strlen(t[i].in), NULL);
		CHECK_UINT(s.limit, t[i].limit);
		CHECK_UINT(s.best_dist, t[i].limit + 1);
		CHECK(s.best == NULL);
		CHECK(s.metric == arg__distance);
		CHECK(s.input == t[i].in);
		CHECK_UINT(s.input_len, strlen(t[i].in));
		CHECK(arg__suggest_best(&s) == NULL);
	}
}

TEST(suggest_typos)
{
	CHECK_STR(suggest("jbos", opts), "jobs");
	CHECK_STR(suggest("verbsoe", opts), "verbose");
	CHECK_STR(suggest("instal", opts), "install");
	CHECK_STR(suggest("hlep", opts), "help");
	CHECK_STR(suggest("outptu", opts), "output");
	CHECK_STR(suggest("verison", opts), "version");
	CHECK_STR(suggest("jobz", opts), "jobs");
}

TEST(suggest_returns_candidate_pointer)
{
	struct arg__suggester s;

	arg__suggest_init(&s, "jbos", 4, NULL);
	arg__suggest_add(&s, opts[0]);
	CHECK(arg__suggest_best(&s) == opts[0]);
	CHECK_UINT(s.best_dist, 1);
}

TEST(suggest_exact_match)
{
	CHECK_STR(suggest("jobs", opts), "jobs");
	CHECK_STR(suggest("verbose", opts), "verbose");
}

TEST(suggest_prefix_rule)
{
	static const char *const typo_first[] = { "verbose", "verd", NULL };
	static const char *const tie[] = { "verbose", "herbs", NULL };
	static const char *const tie_rev[] = { "herbs", "verbose", NULL };
	struct arg__suggester s;

	/* verb -> verbose has distance 3 > limit 2, but is a prefix. */
	CHECK_STR(suggest("verb", opts), "verbose");
	CHECK_STR(suggest("inst", opts), "install");
	CHECK_STR(suggest("outp", opts), "output");
	/* Three bytes is enough (limit 1). */
	CHECK_STR(suggest("ver", opts), "verbose");
	/* Prefix counts as exactly the limit. */
	arg__suggest_init(&s, "verb", 4, NULL);
	arg__suggest_add(&s, "verbose");
	CHECK_UINT(s.best_dist, 2);
	/* A real typo with a smaller distance still wins. */
	CHECK_STR(suggest("verb", typo_first), "verd");
	/* Equal distance: declaration order decides. */
	CHECK_STR(suggest("verb", tie), "verbose");
	CHECK_STR(suggest("verb", tie_rev), "herbs");
}

TEST(suggest_prefix_needs_three_bytes)
{
	static const char *const c[] = { "verbose", NULL };

	CHECK(suggest("ve", c) == NULL);
	CHECK(suggest("v", c) == NULL);
	CHECK(suggest("", c) == NULL);
}

TEST(suggest_prefix_is_strict_and_case_sensitive)
{
	static const char *const c[] = { "verbose", NULL };
	static const char *const same[] = { "abc", NULL };

	/* Not a prefix: different case. */
	CHECK(suggest("VERB", c) == NULL);
	/* Input longer than the candidate is no prefix. */
	CHECK(suggest("verbosely-yes", c) == NULL);
	/* Equal strings are an exact match, not a prefix. */
	CHECK_STR(suggest("abc", same), "abc");
}

TEST(suggest_too_far)
{
	static const char *const c[] = { "verbose", NULL };

	CHECK(suggest("xyz", c) == NULL);
	CHECK(suggest("xyz", opts) == NULL);
	CHECK(suggest("frobnicate", opts) == NULL);
	CHECK(suggest("zzzzzzzz", opts) == NULL);
}

TEST(suggest_short_inputs)
{
	static const char *const c[] = { "ab", "abcd", NULL };
	static const char *const one[] = { "k", NULL };

	/* Limit 1 for short input. */
	CHECK_STR(suggest("j", one), "k");
	CHECK_STR(suggest("ac", c), "ab");
	CHECK(suggest("xy", c) == NULL);
	/* "bb" -> "k" would need 2. */
	CHECK(suggest("bb", one) == NULL);
}

TEST(suggest_limit_boundaries)
{
	static const char *const c4[] = { "abcd", NULL };
	static const char *const c7[] = { "abcdefg", NULL };

	/* n = 4, limit 2. */
	CHECK_STR(suggest("abxy", c4), "abcd");
	CHECK(suggest("axyz", c4) == NULL);
	/* n = 7, limit 3. */
	CHECK_STR(suggest("abcdxyz", c7), "abcdefg");
	CHECK(suggest("abcwxyz", c7) == NULL);
}

TEST(suggest_tie_first_wins)
{
	static const char *const c1[] = { "jobs", "jabs", "jibs", NULL };
	static const char *const c2[] = { "jibs", "jabs", "jobs", NULL };
	static const char *const c3[] = { "far", "jabs", "jobs", NULL };

	CHECK_STR(suggest("jubs", c1), "jobs");
	CHECK_STR(suggest("jubs", c2), "jibs");
	CHECK_STR(suggest("jubs", c3), "jabs");
}

TEST(suggest_better_later_candidate_wins)
{
	static const char *const c[] = { "jabz", "jobz", "jobs", NULL };

	CHECK_STR(suggest("jobs", c), "jobs");
	CHECK_STR(suggest("jibs", c), "jobs");
}

TEST(suggest_ignores_null_and_empty)
{
	struct arg__suggester s;

	arg__suggest_init(&s, "a", 1, NULL);
	arg__suggest_add(&s, NULL);
	arg__suggest_add(&s, "");
	CHECK(arg__suggest_best(&s) == NULL);
	arg__suggest_add(&s, "b");
	arg__suggest_add(&s, NULL);
	arg__suggest_add(&s, "");
	CHECK_STR(arg__suggest_best(&s), "b");
}

TEST(suggest_empty_input)
{
	static const char *const c[] = { "ab", "a", "b", NULL };
	struct arg__suggester s;

	CHECK_STR(suggest("", c), "a");
	arg__suggest_init(&s, NULL, 0, NULL);
	arg__suggest_add(&s, "x");
	CHECK_STR(arg__suggest_best(&s), "x");
}

TEST(suggest_long_names)
{
	char in[ARG__SUGGEST_MAX_LEN + 2];
	char cand[ARG__SUGGEST_MAX_LEN + 3];
	struct arg__suggester s;

	memset(in, 'a', sizeof(in) - 1);
	in[sizeof(in) - 1] = '\0';
	memcpy(cand, in, sizeof(in));
	cand[sizeof(in) - 1] = 'b';
	cand[sizeof(in)] = '\0';

	/* Long input: the metric refuses, but a strict prefix still fits. */
	arg__suggest_init(&s, in, strlen(in), NULL);
	arg__suggest_add(&s, in);
	CHECK(arg__suggest_best(&s) == NULL);
	arg__suggest_add(&s, cand);
	CHECK(arg__suggest_best(&s) == cand);

	/* A long candidate is only reachable through the prefix rule. */
	arg__suggest_init(&s, "aaaa", 4, NULL);
	arg__suggest_add(&s, cand);
	CHECK(arg__suggest_best(&s) == cand);
	arg__suggest_init(&s, "zzzz", 4, NULL);
	arg__suggest_add(&s, cand);
	CHECK(arg__suggest_best(&s) == NULL);
}

/* ------------------------------------------------------------------ */
/* Custom metric hook                                                 */
/* ------------------------------------------------------------------ */

static unsigned metric_calls;
static unsigned metric_last_max;

/* Case-insensitive ASCII metric: 0 when equal ignoring case. */
static unsigned ci_metric(const char *a, size_t an, const char *b, size_t bn,
			  unsigned max)
{
	size_t i;

	metric_calls++;
	metric_last_max = max;
	if (an != bn)
		return max + 1;
	for (i = 0; i < an; i++) {
		char x = a[i], y = b[i];

		if (x >= 'A' && x <= 'Z')
			x = (char)(x - 'A' + 'a');
		if (y >= 'A' && y <= 'Z')
			y = (char)(y - 'A' + 'a');
		if (x != y)
			return max + 1;
	}
	return 0;
}

TEST(suggest_custom_metric)
{
	struct arg__suggester s;

	metric_calls = 0;
	arg__suggest_init(&s, "JOBS", 4, ci_metric);
	CHECK(s.metric == ci_metric);
	arg__suggest_add(&s, "verbose");
	arg__suggest_add(&s, NULL);
	arg__suggest_add(&s, "");
	arg__suggest_add(&s, "jobs");
	arg__suggest_add(&s, "Jobs");
	CHECK_UINT(metric_calls, 3);
	CHECK_UINT(metric_last_max, 2);
	CHECK_STR(arg__suggest_best(&s), "jobs");

	/* The default metric would have taken this typo; ours does not. */
	metric_calls = 0;
	arg__suggest_init(&s, "jbos", 4, ci_metric);
	arg__suggest_add(&s, "jobs");
	CHECK_UINT(metric_calls, 1);
	CHECK(arg__suggest_best(&s) == NULL);
}

TEST(default_metric_fits_hook_type)
{
	arg_suggest_fn f = arg__distance;

	CHECK_UINT(f("ab", 2, "ba", 2, 3), 1);
}

TEST_MAIN(TEST_CASE(distance_table),
	  TEST_CASE(distance_symmetric),
	  TEST_CASE(distance_exact_bound_is_kept),
	  TEST_CASE(distance_bounded_early_exit),
	  TEST_CASE(distance_max_saturates),
	  TEST_CASE(distance_length_limit),
	  TEST_CASE(distance_not_nul_terminated),
	  TEST_CASE(distance_matches_reference),
	  TEST_CASE(suggest_init_limits),
	  TEST_CASE(suggest_typos),
	  TEST_CASE(suggest_returns_candidate_pointer),
	  TEST_CASE(suggest_exact_match),
	  TEST_CASE(suggest_prefix_rule),
	  TEST_CASE(suggest_prefix_needs_three_bytes),
	  TEST_CASE(suggest_prefix_is_strict_and_case_sensitive),
	  TEST_CASE(suggest_too_far),
	  TEST_CASE(suggest_short_inputs),
	  TEST_CASE(suggest_limit_boundaries),
	  TEST_CASE(suggest_tie_first_wins),
	  TEST_CASE(suggest_better_later_candidate_wins),
	  TEST_CASE(suggest_ignores_null_and_empty),
	  TEST_CASE(suggest_empty_input),
	  TEST_CASE(suggest_long_names),
	  TEST_CASE(suggest_custom_metric),
	  TEST_CASE(default_metric_fits_hook_type))
