// SPDX-License-Identifier: GPL-2.0-only
/*
 * suggest.c - "did you mean ...?" engine (SDD 8.5, 15)
 *
 * The default metric is the optimal string alignment distance
 * (Levenshtein plus transposition of two adjacent bytes, every edit
 * costs 1). It is byte-wise and case-sensitive, works on at most
 * ARG__SUGGEST_MAX_LEN bytes per string, keeps three rows on the
 * stack and gives up as soon as the result must exceed the bound.
 * It only ever runs after a parse error, never on the success path.
 */
#include "arg_internal.h"

#include <limits.h>

/* The "too far" result for a bound; saturates instead of wrapping. */
static unsigned over(unsigned max)
{
	return max == UINT_MAX ? UINT_MAX : max + 1;
}

static unsigned min3(unsigned x, unsigned y, unsigned z)
{
	unsigned m = x < y ? x : y;

	return m < z ? m : z;
}

unsigned arg__distance(const char *a, size_t an, const char *b, size_t bn,
		       unsigned max)
{
	unsigned rows[3][ARG__SUGGEST_MAX_LEN + 1];
	unsigned *pp, *prev, *cur, *tmp;
	size_t diff, i, j;

	if (an > ARG__SUGGEST_MAX_LEN || bn > ARG__SUGGEST_MAX_LEN)
		return over(max);
	diff = an > bn ? an - bn : bn - an;
	if (diff > max)
		return over(max);
	if (an == 0 || bn == 0)
		return (unsigned)diff;

	pp = rows[0];
	prev = rows[1];
	cur = rows[2];
	for (j = 0; j <= bn; j++)
		prev[j] = (unsigned)j;

	for (i = 1; i <= an; i++) {
		unsigned row_min;

		cur[0] = (unsigned)i;
		row_min = cur[0];
		for (j = 1; j <= bn; j++) {
			unsigned cost = a[i - 1] == b[j - 1] ? 0u : 1u;
			unsigned d = min3(prev[j] + 1, cur[j - 1] + 1,
					  prev[j - 1] + cost);

			if (i > 1 && j > 1 && a[i - 1] == b[j - 2] &&
			    a[i - 2] == b[j - 1] && pp[j - 2] + 1 < d)
				d = pp[j - 2] + 1;
			cur[j] = d;
			if (d < row_min)
				row_min = d;
		}
		/*
		 * A cell is at most one more than the cell above it, so
		 * once a whole row is past max, no later row (nor a
		 * transposition reaching back two rows) can come back.
		 */
		if (row_min > max)
			return over(max);
		tmp = pp;
		pp = prev;
		prev = cur;
		cur = tmp;
	}
	return prev[bn] > max ? over(max) : prev[bn];
}
