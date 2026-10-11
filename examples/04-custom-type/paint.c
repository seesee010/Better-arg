// SPDX-License-Identifier: GPL-2.0-only
/*
 * paint.c - a custom value type, enums and a command rule (SDD 8.4, 10)
 *
 * A type is a name, a size and a parse function. Defaults go through
 * the same parse function, and arg_get() copies the value out.
 *
 *	$ paint --color '#00ff7f' --format jpg --fill
 *	$ paint --color red              # error: expected #RRGGBB
 *	$ paint --fill --outline          # error: rule violated
 */
#include <stdint.h>
#include <stdio.h>

#include "arg.h"

/* "#RRGGBB" -> 0xRRGGBB */
static int parse_rgb(const arg_type *t, const char *s, size_t n, void *dst,
		     arg_ctx *ctx)
{
	uint32_t v = 0;
	size_t i;

	(void)t;
	if (n != 7 || s[0] != '#')
		return arg_reject(ctx, "expected #RRGGBB");
	for (i = 1; i < 7; i++) {
		int d = arg_hexval(s[i]);

		if (d < 0)
			return arg_reject(ctx, "invalid hex digit");
		v = (v << 4) | (uint32_t)d;
	}
	*(uint32_t *)dst = v;
	return 0;
}

static const arg_type t_rgb = {
	.name = "COLOR", .size = sizeof(uint32_t), .parse = parse_rgb
};

/* choices of --format; arg_enum() returns an index into it */
static const char *const formats[] = { "png", "jpg", "webp", NULL };

static int paint(arg *a)
{
	const char *style = "plain";
	uint32_t accent;

	if (arg_flag(a, "fill"))
		style = "filled";
	else if (arg_flag(a, "outline"))
		style = "outlined";
	arg_get(a, "color", &accent);
	printf("accent #%06lx, %s, %s\n", (unsigned long)accent,
	       formats[arg_enum(a, "format")],
	       style);
	return 0;
}

int main(int argc, char **argv)
{
	return ARG_RUN(argc, argv, "paint", "Paints a shape.",
		ARG_CUSTOM('c', "color", &t_rgb, "Accent colour",
			   .def = "#ff8800"),
		ARG_ENUM('f', "format", "Output format", formats,
			 .def = "png"),
		ARG_FLAG(0, "fill", "Fill the shape"),
		ARG_FLAG(0, "outline", "Only draw the outline"),
		ARG_MUTEX("fill", "outline"),
		ARG_MAIN(paint));
}
