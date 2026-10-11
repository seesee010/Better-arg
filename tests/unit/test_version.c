// SPDX-License-Identifier: GPL-2.0-only
/*
 * test_version.c - smoke tests for the build and the harness
 */
#include "arg.h"
#include "test.h"

TEST(version_string_matches_macros)
{
	char want[32];

	snprintf(want, sizeof(want), "%d.%d.%d", ARG_VERSION_MAJOR,
		 ARG_VERSION_MINOR, ARG_VERSION_PATCH);
	CHECK_STR(arg_version(), want);
	CHECK_STR(ARG_VERSION_STRING, want);
}

TEST(no_handler_returns_zero)
{
	CHECK_INT(arg_no_handler(NULL), 0);
}

TEST(alloc_stdlib_grows_and_frees)
{
	char *p = arg_alloc_stdlib(NULL, NULL, 0, 16);

	REQUIRE(p != NULL);
	memset(p, 'x', 16);
	p = arg_alloc_stdlib(NULL, p, 16, 64);
	REQUIRE(p != NULL);
	CHECK_INT(p[15], 'x');
	CHECK(arg_alloc_stdlib(NULL, p, 64, 0) == NULL);
}

TEST(parser_fits_on_the_stack)
{
	CHECK(sizeof(arg_parser) >= ARG_INLINE_ARENA);
	CHECK(sizeof(arg_parser) < 4096);
}

TEST_MAIN(TEST_CASE(version_string_matches_macros),
	  TEST_CASE(no_handler_returns_zero),
	  TEST_CASE(alloc_stdlib_grows_and_frees),
	  TEST_CASE(parser_fits_on_the_stack))
