// SPDX-License-Identifier: GPL-2.0-only
/*
 * types.c - built-in value types and decorator types (SDD 8.2, 8.3)
 *
 * Every converter reads only s[0..n): the input is not necessarily
 * NUL-terminated. The one exception is storing the text itself: the
 * string type and ARG_ONE_OF_STR keep the pointer s, so for those the
 * caller guarantees s[n] == '\0' (argv strings and the parser's own
 * copies always are).
 *
 * Integer, size and duration parsing is done here with checked
 * arithmetic and ASCII helpers only: no strtol, no errno, no locale.
 * Floating point input is validated against a strict grammar first and
 * then converted with strtod on a bounded local copy, with the decimal
 * point swapped for the current locale's one.
 *
 * On failure a converter never writes to *dst.
 */
#include "arg_internal.h"

#include <float.h>

#ifndef ARG_NO_FLOAT
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#endif

/* ------------------------------------------------------------------ */
/* Small ASCII helpers                                                */
/* ------------------------------------------------------------------ */

static bool is_digit(char c)
{
	return c >= '0' && c <= '9';
}

static char lower(char c)
{
	return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

int arg_hexval(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

bool arg__ieq(const char *a, size_t an, const char *b)
{
	size_t i;

	for (i = 0; i < an; i++) {
		if (b[i] == '\0' || lower(a[i]) != lower(b[i]))
			return false;
	}
	return b[an] == '\0';
}

/* Exact, case-sensitive equality of s[0..n) with NUL-terminated w. */
static bool word_eq(const char *s, size_t n, const char *w)
{
	return strlen(w) == n && memcmp(s, w, n) == 0;
}

/* ------------------------------------------------------------------ */
/* Rejection and the stand-alone converter                            */
/* ------------------------------------------------------------------ */

int arg_reject(arg_ctx *ctx, const char *msg)
{
	return arg_reject_status(ctx, ARG_E_BAD_VALUE, msg);
}

int arg_reject_status(arg_ctx *ctx, arg_status st, const char *msg)
{
	if (ctx) {
		ctx->detail = msg;
		ctx->status = st;
	}
	return -1;
}

arg_status arg_type_parse(const arg_type *t, const char *text, void *dst,
			  const char **detail)
{
	arg_ctx ctx = { NULL, NULL, NULL, ARG_E_BAD_VALUE };

	if (!t || !t->parse || !text) {
		if (detail)
			*detail = "invalid argument";
		return ARG_E_BAD_VALUE;
	}
	if (t->parse(t, text, strlen(text), dst, &ctx) == 0) {
		if (detail)
			*detail = NULL;
		return ARG_OK;
	}
	if (detail)
		*detail = ctx.detail;
	/* a parse function that forgot to set a failure status */
	return ctx.status == ARG_OK ? ARG_E_BAD_VALUE : ctx.status;
}

/* ------------------------------------------------------------------ */
/* Bounded output for format callbacks                                */
/* ------------------------------------------------------------------ */

struct sbuf {
	char *buf;
	size_t cap;
	size_t len; /* full length, may exceed cap */
};

static void sb_put(struct sbuf *b, const char *s, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++, b->len++) {
		if (b->len + 1 < b->cap)
			b->buf[b->len] = s[i];
	}
}

static void sb_puts(struct sbuf *b, const char *s)
{
	sb_put(b, s, strlen(s));
}

/* Decimal digits of v; returns the length, no NUL. buf needs 20 bytes. */
static size_t utoa10(uint64_t v, char *buf)
{
	char tmp[20];
	size_t n = 0, i;

	do {
		tmp[n++] = (char)('0' + (int)(v % 10u));
		v /= 10u;
	} while (v);
	for (i = 0; i < n; i++)
		buf[i] = tmp[n - 1 - i];
	return n;
}

static void sb_putu(struct sbuf *b, uint64_t v)
{
	char d[20];

	sb_put(b, d, utoa10(v, d));
}

/* NUL-terminate and return the full length. */
static int sb_end(struct sbuf *b)
{
	if (b->cap) {
		if (b->len < b->cap)
			b->buf[b->len] = '\0';
		else
			b->buf[b->cap - 1] = '\0';
	}
	return b->len > (size_t)INT32_MAX ? -1 : (int)b->len;
}

static int format_str(char *buf, size_t cap, const char *s)
{
	struct sbuf b = { buf, cap, 0 };

	sb_puts(&b, s);
	return sb_end(&b);
}

/* ------------------------------------------------------------------ */
/* Typed loads (arg_internal.h)                                       */
/* ------------------------------------------------------------------ */

bool arg__load_int(const arg_type *t, const void *src, int64_t *out)
{
	if (!t || !src)
		return false;
	if (t->kind == ARG_K_INT) {
		switch (t->size) {
		case 1: {
			int8_t v;

			memcpy(&v, src, sizeof(v));
			*out = v;
			return true;
		}
		case 2: {
			int16_t v;

			memcpy(&v, src, sizeof(v));
			*out = v;
			return true;
		}
		case 4: {
			int32_t v;

			memcpy(&v, src, sizeof(v));
			*out = v;
			return true;
		}
		case 8: {
			int64_t v;

			memcpy(&v, src, sizeof(v));
			*out = v;
			return true;
		}
		default:
			return false;
		}
	}
	if (t->kind == ARG_K_ENUM) {
		int v;

		memcpy(&v, src, sizeof(v));
		*out = v;
		return true;
	}
	if (t->kind == ARG_K_BOOL) {
		bool v;

		memcpy(&v, src, sizeof(v));
		*out = v ? 1 : 0;
		return true;
	}
	return false;
}

bool arg__load_uint(const arg_type *t, const void *src, uint64_t *out)
{
	if (!t || !src || t->kind != ARG_K_UINT)
		return false;
	switch (t->size) {
	case 1: {
		uint8_t v;

		memcpy(&v, src, sizeof(v));
		*out = v;
		return true;
	}
	case 2: {
		uint16_t v;

		memcpy(&v, src, sizeof(v));
		*out = v;
		return true;
	}
	case 4: {
		uint32_t v;

		memcpy(&v, src, sizeof(v));
		*out = v;
		return true;
	}
	case 8:
		memcpy(out, src, sizeof(*out));
		return true;
	default:
		return false;
	}
}

bool arg__load_double(const arg_type *t, const void *src, double *out)
{
	if (!t || !src || t->kind != ARG_K_DOUBLE)
		return false;
	if (t->size == sizeof(double)) {
		memcpy(out, src, sizeof(*out));
		return true;
	}
	if (t->size == sizeof(float)) {
		float f;

		memcpy(&f, src, sizeof(f));
		*out = (double)f;
		return true;
	}
	return false;
}

bool arg__compare(const arg_type *t, const void *a, const void *b, int *cmp)
{
	if (!t)
		return false;
	switch (t->kind) {
	case ARG_K_INT: {
		int64_t x, y;

		if (!arg__load_int(t, a, &x) || !arg__load_int(t, b, &y))
			return false;
		*cmp = (x > y) - (x < y);
		return true;
	}
	case ARG_K_UINT: {
		uint64_t x, y;

		if (!arg__load_uint(t, a, &x) || !arg__load_uint(t, b, &y))
			return false;
		*cmp = (x > y) - (x < y);
		return true;
	}
	case ARG_K_DOUBLE: {
		double x, y;

		if (!arg__load_double(t, a, &x) ||
		    !arg__load_double(t, b, &y))
			return false;
		if (x != x || y != y)
			return false; /* NaN: unordered */
		*cmp = (x > y) - (x < y);
		return true;
	}
	default:
		return false;
	}
}

/* ------------------------------------------------------------------ */
/* bool                                                               */
/* ------------------------------------------------------------------ */

static int bool_parse(const arg_type *t, const char *s, size_t n, void *dst,
		      arg_ctx *ctx)
{
	static const char *const yes[] = { "true", "yes", "on", "1" };
	static const char *const no[] = { "false", "no", "off", "0" };
	bool v;
	size_t i;

	(void)t;
	for (i = 0; i < ARG__COUNTOF(yes); i++) {
		if (arg__ieq(s, n, yes[i])) {
			v = true;
			goto store;
		}
		if (arg__ieq(s, n, no[i])) {
			v = false;
			goto store;
		}
	}
	return arg_reject(ctx, "expected true/false, yes/no, on/off or 1/0");
store:
	memcpy(dst, &v, sizeof(v));
	return 0;
}

static int bool_format(const arg_type *t, const void *src, char *buf,
		       size_t cap)
{
	bool v;

	(void)t;
	memcpy(&v, src, sizeof(v));
	return format_str(buf, cap, v ? "true" : "false");
}

const arg_type arg_t_bool = {
	.name = "BOOL",
	.size = sizeof(bool),
	.parse = bool_parse,
	.format = bool_format,
	.kind = ARG_K_BOOL,
};

/* ------------------------------------------------------------------ */
/* Integers                                                           */
/* ------------------------------------------------------------------ */

#define MSG_INT "expected an integer"
#define MSG_UINT "expected a non-negative integer"
#define MSG_RANGE "out of range"

/*
 * Scan the magnitude of an integer literal without sign: decimal, 0x,
 * 0o or 0b, with '_' allowed only between two digits. Returns false on
 * a syntax error. *ovf is set when the value does not fit uint64_t
 * (the syntax is still checked to the end).
 */
static bool scan_mag(const char *s, size_t n, uint64_t *out, bool *ovf)
{
	unsigned base = 10;
	uint64_t v = 0;
	size_t i = 0;
	bool any = false, prev_digit = false;

	*ovf = false;
	if (n >= 2 && s[0] == '0') {
		switch (s[1]) {
		case 'x':
		case 'X':
			base = 16;
			break;
		case 'o':
		case 'O':
			base = 8;
			break;
		case 'b':
		case 'B':
			base = 2;
			break;
		default:
			break;
		}
		if (base != 10)
			i = 2;
	}
	for (; i < n; i++) {
		int d;

		if (s[i] == '_') {
			/* needs a digit before and after */
			if (!prev_digit || i + 1 >= n)
				return false;
			prev_digit = false;
			continue;
		}
		d = arg_hexval(s[i]);
		if (d < 0 || (unsigned)d >= base)
			return false;
		if (v > (UINT64_MAX - (unsigned)d) / base)
			*ovf = true;
		else
			v = v * base + (unsigned)d;
		any = true;
		prev_digit = true;
	}
	if (!any || !prev_digit)
		return false;
	*out = v;
	return true;
}

static int int_parse(const arg_type *t, const char *s, size_t n, void *dst,
		     arg_ctx *ctx)
{
	uint64_t mag, lim;
	int64_t v;
	bool neg = false, ovf;
	unsigned bits;

	if (t->size != 1 && t->size != 2 && t->size != 4 && t->size != 8)
		return arg_reject(ctx, "unsupported integer size");
	bits = (unsigned)t->size * 8u;
	if (n && (s[0] == '+' || s[0] == '-')) {
		neg = s[0] == '-';
		s++;
		n--;
	}
	if (!scan_mag(s, n, &mag, &ovf))
		return arg_reject(ctx, MSG_INT);
	lim = ((uint64_t)1 << (bits - 1)) - 1u; /* type max */
	if (ovf || mag > lim + (neg ? 1u : 0u))
		return arg_reject_status(ctx, ARG_E_RANGE, MSG_RANGE);
	if (!neg)
		v = (int64_t)mag;
	else if (mag == (uint64_t)1 << 63)
		v = INT64_MIN;
	else
		v = -(int64_t)mag;

	switch (t->size) {
	case 1: {
		int8_t x = (int8_t)v;

		memcpy(dst, &x, sizeof(x));
		break;
	}
	case 2: {
		int16_t x = (int16_t)v;

		memcpy(dst, &x, sizeof(x));
		break;
	}
	case 4: {
		int32_t x = (int32_t)v;

		memcpy(dst, &x, sizeof(x));
		break;
	}
	default:
		memcpy(dst, &v, sizeof(v));
		break;
	}
	return 0;
}

static int uint_parse(const arg_type *t, const char *s, size_t n, void *dst,
		      arg_ctx *ctx)
{
	uint64_t mag, lim;
	bool ovf;

	if (t->size != 1 && t->size != 2 && t->size != 4 && t->size != 8)
		return arg_reject(ctx, "unsupported integer size");
	if (n && s[0] == '-')
		return arg_reject(ctx, MSG_UINT);
	if (n && s[0] == '+') {
		s++;
		n--;
	}
	if (!scan_mag(s, n, &mag, &ovf))
		return arg_reject(ctx, MSG_INT);
	lim = t->size == 8 ? UINT64_MAX
			   : ((uint64_t)1 << (t->size * 8u)) - 1u;
	if (ovf || mag > lim)
		return arg_reject_status(ctx, ARG_E_RANGE, MSG_RANGE);

	switch (t->size) {
	case 1: {
		uint8_t x = (uint8_t)mag;

		memcpy(dst, &x, sizeof(x));
		break;
	}
	case 2: {
		uint16_t x = (uint16_t)mag;

		memcpy(dst, &x, sizeof(x));
		break;
	}
	case 4: {
		uint32_t x = (uint32_t)mag;

		memcpy(dst, &x, sizeof(x));
		break;
	}
	default:
		memcpy(dst, &mag, sizeof(mag));
		break;
	}
	return 0;
}

static int int_format(const arg_type *t, const void *src, char *buf,
		      size_t cap)
{
	struct sbuf b = { buf, cap, 0 };
	int64_t v;

	if (!arg__load_int(t, src, &v))
		return -1;
	if (v < 0) {
		sb_put(&b, "-", 1);
		/* -(v + 1) + 1 avoids overflow for INT64_MIN */
		sb_putu(&b, (uint64_t)(-(v + 1)) + 1u);
	} else {
		sb_putu(&b, (uint64_t)v);
	}
	return sb_end(&b);
}

static int uint_format(const arg_type *t, const void *src, char *buf,
		       size_t cap)
{
	struct sbuf b = { buf, cap, 0 };
	uint64_t v;

	if (!arg__load_uint(t, src, &v))
		return -1;
	sb_putu(&b, v);
	return sb_end(&b);
}

#define ARG__INT_TYPE(name_, ctype_)                                         \
	const arg_type name_ = {                                             \
		.name = "N",                                                 \
		.size = sizeof(ctype_),                                      \
		.parse = int_parse,                                          \
		.format = int_format,                                        \
		.kind = ARG_K_INT,                                           \
	}
#define ARG__UINT_TYPE(name_, ctype_)                                        \
	const arg_type name_ = {                                             \
		.name = "N",                                                 \
		.size = sizeof(ctype_),                                      \
		.parse = uint_parse,                                         \
		.format = uint_format,                                       \
		.kind = ARG_K_UINT,                                          \
	}

ARG__INT_TYPE(arg_t_i8, int8_t);
ARG__INT_TYPE(arg_t_i16, int16_t);
ARG__INT_TYPE(arg_t_i32, int32_t);
ARG__INT_TYPE(arg_t_i64, int64_t);
ARG__INT_TYPE(arg_t_int, int);
ARG__INT_TYPE(arg_t_long, long);
ARG__UINT_TYPE(arg_t_u8, uint8_t);
ARG__UINT_TYPE(arg_t_u16, uint16_t);
ARG__UINT_TYPE(arg_t_u32, uint32_t);
ARG__UINT_TYPE(arg_t_u64, uint64_t);
ARG__UINT_TYPE(arg_t_uint, unsigned int);
ARG__UINT_TYPE(arg_t_size_t, size_t);

/* ------------------------------------------------------------------ */
/* double / float                                                     */
/* ------------------------------------------------------------------ */

#define MSG_NUM "expected a number"

#ifndef ARG_NO_FLOAT

/* Longest accepted input; longer text is rejected, never truncated. */
#define ARG__NUM_MAX 256

/*
 * Check s[0..n) against
 *   [+-]? (digits [. digits?] | . digits) ([eE] [+-]? digits)?
 * or [+-]? (inf | infinity | nan), ASCII case-insensitive.
 * *special is set for the inf / nan forms.
 */
static bool num_grammar(const char *s, size_t n, bool *special)
{
	size_t i = 0, d1 = 0, d2 = 0, de = 0;

	*special = false;
	if (i < n && (s[i] == '+' || s[i] == '-'))
		i++;
	if (arg__ieq(s + i, n - i, "inf") ||
	    arg__ieq(s + i, n - i, "infinity") ||
	    arg__ieq(s + i, n - i, "nan")) {
		*special = true;
		return true;
	}
	for (; i < n && is_digit(s[i]); i++)
		d1++;
	if (i < n && s[i] == '.') {
		for (i++; i < n && is_digit(s[i]); i++)
			d2++;
	}
	if (d1 + d2 == 0)
		return false;
	if (i < n && (s[i] == 'e' || s[i] == 'E')) {
		i++;
		if (i < n && (s[i] == '+' || s[i] == '-'))
			i++;
		for (; i < n && is_digit(s[i]); i++)
			de++;
		if (!de)
			return false;
	}
	return i == n;
}

/* Convert a grammar-checked number; returns false if it is too long. */
static bool num_convert(const char *s, size_t n, double *out)
{
	char tmp[ARG__NUM_MAX + 8];
	const char *dp = localeconv()->decimal_point;
	size_t dpn = dp ? strlen(dp) : 0, i, j = 0;
	char *end;

	if (dpn == 0) {
		dp = ".";
		dpn = 1;
	}
	if (n > ARG__NUM_MAX || dpn > 4)
		return false;
	for (i = 0; i < n; i++) {
		if (s[i] == '.') {
			memcpy(tmp + j, dp, dpn);
			j += dpn;
		} else {
			tmp[j++] = s[i];
		}
	}
	tmp[j] = '\0';
	*out = strtod(tmp, &end);
	return end == tmp + j;
}

static int double_parse(const arg_type *t, const char *s, size_t n, void *dst,
			arg_ctx *ctx)
{
	bool special;
	double d;

	if (!num_grammar(s, n, &special) || !num_convert(s, n, &d))
		return arg_reject(ctx, MSG_NUM);
	if (!special && (d > DBL_MAX || d < -DBL_MAX))
		return arg_reject_status(ctx, ARG_E_RANGE, MSG_RANGE);
	if (t->size == sizeof(float)) {
		float f;

		if (!special && (d > FLT_MAX || d < -FLT_MAX))
			return arg_reject_status(ctx, ARG_E_RANGE, MSG_RANGE);
		f = (float)d;
		memcpy(dst, &f, sizeof(f));
	} else {
		memcpy(dst, &d, sizeof(d));
	}
	return 0;
}

/* Replace the locale's decimal point in s by '.', in place. */
static void dot_from_locale(char *s)
{
	const char *dp = localeconv()->decimal_point;
	size_t dpn = dp ? strlen(dp) : 0;
	char *p;

	if (dpn == 0 || (dpn == 1 && dp[0] == '.'))
		return;
	p = strstr(s, dp);
	if (p) {
		*p = '.';
		memmove(p + 1, p + dpn, strlen(p + dpn) + 1);
	}
}

/*
 * Shortest "%.*g" text that converts back to the same value, then made
 * locale independent.
 */
static int double_format(const arg_type *t, const void *src, char *buf,
			 size_t cap)
{
	char tmp[64];
	double v;
	bool is_float = t->size == sizeof(float);
	int p, maxp = is_float ? 9 : 17;

	if (!arg__load_double(t, src, &v))
		return -1;
	for (p = 1; p <= maxp; p++) {
		double back;

		snprintf(tmp, sizeof(tmp), "%.*g", p, v);
		back = strtod(tmp, NULL);
		if (is_float ? (float)back == (float)v : back == v)
			break;
	}
	if (p > maxp) /* NaN never compares equal */
		snprintf(tmp, sizeof(tmp), "%.*g", maxp, v);
	dot_from_locale(tmp);
	return format_str(buf, cap, tmp);
}

#else /* ARG_NO_FLOAT */

static int double_parse(const arg_type *t, const char *s, size_t n, void *dst,
			arg_ctx *ctx)
{
	(void)t;
	(void)s;
	(void)n;
	(void)dst;
	return arg_reject(ctx, "floating point disabled");
}

static int double_format(const arg_type *t, const void *src, char *buf,
			 size_t cap)
{
	(void)t;
	(void)src;
	(void)buf;
	(void)cap;
	return -1;
}

#endif /* ARG_NO_FLOAT */

const arg_type arg_t_double = {
	.name = "NUM",
	.size = sizeof(double),
	.parse = double_parse,
	.format = double_format,
	.kind = ARG_K_DOUBLE,
};

const arg_type arg_t_float = {
	.name = "NUM",
	.size = sizeof(float),
	.parse = double_parse,
	.format = double_format,
	.kind = ARG_K_DOUBLE,
};

/* ------------------------------------------------------------------ */
/* string                                                             */
/* ------------------------------------------------------------------ */

/* Stores s itself; the caller guarantees s[n] == '\0'. */
static int string_parse(const arg_type *t, const char *s, size_t n,
			void *dst, arg_ctx *ctx)
{
	(void)t;
	(void)n;
	(void)ctx;
	memcpy(dst, &s, sizeof(s));
	return 0;
}

static int string_format(const arg_type *t, const void *src, char *buf,
			 size_t cap)
{
	const char *s;

	(void)t;
	memcpy(&s, src, sizeof(s));
	if (!s)
		return -1;
	return format_str(buf, cap, s);
}

const arg_type arg_t_string = {
	.name = "TEXT",
	.size = sizeof(const char *),
	.parse = string_parse,
	.format = string_format,
	.hints = 0,
	.kind = ARG_K_STR,
};

/* ------------------------------------------------------------------ */
/* enum                                                               */
/* ------------------------------------------------------------------ */

static int enum_parse(const arg_type *t, const char *s, size_t n, void *dst,
		      arg_ctx *ctx)
{
	const char *const *names;
	int i;

	(void)t;
	if (!ctx || !ctx->item || !ctx->item->names)
		return arg_reject(ctx, "no choices");
	names = ctx->item->names;
	for (i = 0; names[i]; i++) {
		if (word_eq(s, n, names[i])) {
			memcpy(dst, &i, sizeof(i));
			return 0;
		}
	}
	return arg_reject(ctx, "invalid choice");
}

/* The choices live in the item, which format cannot see. */
static int enum_format(const arg_type *t, const void *src, char *buf,
		       size_t cap)
{
	(void)t;
	(void)src;
	if (cap)
		buf[0] = '\0';
	return -1;
}

const arg_type arg_t_enum = {
	.name = "CHOICE",
	.size = sizeof(int),
	.parse = enum_parse,
	.format = enum_format,
	.kind = ARG_K_ENUM,
};

/* ------------------------------------------------------------------ */
/* bytesize                                                           */
/* ------------------------------------------------------------------ */

#define MSG_SIZE "expected a size such as 512, 4k, 16M or 1KiB"

static const struct {
	const char *suffix;
	uint64_t mul;
} size_units[] = {
	{ "", 1 },
	{ "b", 1 },
	{ "k", UINT64_C(1000) },
	{ "kb", UINT64_C(1000) },
	{ "m", UINT64_C(1000000) },
	{ "mb", UINT64_C(1000000) },
	{ "g", UINT64_C(1000000000) },
	{ "gb", UINT64_C(1000000000) },
	{ "t", UINT64_C(1000000000000) },
	{ "tb", UINT64_C(1000000000000) },
	{ "p", UINT64_C(1000000000000000) },
	{ "pb", UINT64_C(1000000000000000) },
	{ "e", UINT64_C(1000000000000000000) },
	{ "eb", UINT64_C(1000000000000000000) },
	{ "ki", UINT64_C(1) << 10 },
	{ "kib", UINT64_C(1) << 10 },
	{ "mi", UINT64_C(1) << 20 },
	{ "mib", UINT64_C(1) << 20 },
	{ "gi", UINT64_C(1) << 30 },
	{ "gib", UINT64_C(1) << 30 },
	{ "ti", UINT64_C(1) << 40 },
	{ "tib", UINT64_C(1) << 40 },
	{ "pi", UINT64_C(1) << 50 },
	{ "pib", UINT64_C(1) << 50 },
	{ "ei", UINT64_C(1) << 60 },
	{ "eib", UINT64_C(1) << 60 },
};

static int bytesize_parse(const arg_type *t, const char *s, size_t n,
			  void *dst, arg_ctx *ctx)
{
	size_t i = 0, k;
	uint64_t v;
	bool ovf;

	(void)t;
	/* decimal digits and separators; scan_mag checks their placement */
	while (i < n && (is_digit(s[i]) || s[i] == '_'))
		i++;
	if (!scan_mag(s, i, &v, &ovf))
		return arg_reject(ctx, MSG_SIZE);
	for (k = 0; k < ARG__COUNTOF(size_units); k++) {
		if (arg__ieq(s + i, n - i, size_units[k].suffix))
			break;
	}
	if (k == ARG__COUNTOF(size_units))
		return arg_reject(ctx, MSG_SIZE);
	if (ovf || v > UINT64_MAX / size_units[k].mul)
		return arg_reject_status(ctx, ARG_E_RANGE, MSG_RANGE);
	v *= size_units[k].mul;
	memcpy(dst, &v, sizeof(v));
	return 0;
}

static int bytesize_format(const arg_type *t, const void *src, char *buf,
			   size_t cap)
{
	static const char *const bin[] = { "KiB", "MiB", "GiB",
					   "TiB", "PiB", "EiB" };
	struct sbuf b = { buf, cap, 0 };
	uint64_t v;
	unsigned i;

	(void)t;
	memcpy(&v, src, sizeof(v));
	for (i = ARG__COUNTOF(bin); v && i > 0; i--) {
		uint64_t unit = UINT64_C(1) << (10u * i);

		if (v % unit == 0) {
			sb_putu(&b, v / unit);
			sb_puts(&b, bin[i - 1]);
			return sb_end(&b);
		}
	}
	sb_putu(&b, v);
	return sb_end(&b);
}

const arg_type arg_t_bytesize = {
	.name = "SIZE",
	.size = sizeof(uint64_t),
	.parse = bytesize_parse,
	.format = bytesize_format,
	.kind = ARG_K_UINT,
};

/* ------------------------------------------------------------------ */
/* duration                                                           */
/* ------------------------------------------------------------------ */

#define MSG_DUR "expected a duration such as 250ms, 2s, 1m30s or 1h"

static const struct {
	const char *unit;
	uint64_t ms;
} dur_units[] = {
	/* decreasing order; the index is the rank */
	{ "d", UINT64_C(86400000) },
	{ "h", UINT64_C(3600000) },
	{ "m", UINT64_C(60000) },
	{ "s", UINT64_C(1000) },
	{ "ms", UINT64_C(1) },
};

static int duration_parse(const arg_type *t, const char *s, size_t n,
			  void *dst, arg_ctx *ctx)
{
	uint64_t total = 0;
	size_t i = 0, next_rank = 0;
	bool ovf = false;

	(void)t;
	if (n == 0)
		return arg_reject(ctx, MSG_DUR);
	while (i < n) {
		uint64_t v = 0, part;
		size_t start = i, u, ulen, r;

		for (; i < n && is_digit(s[i]); i++) {
			unsigned d = (unsigned)(s[i] - '0');

			if (v > (UINT64_MAX - d) / 10u)
				ovf = true;
			else
				v = v * 10u + d;
		}
		if (i == start)
			return arg_reject(ctx, MSG_DUR);
		if (i == n && start == 0) {
			/* a bare number means seconds */
			r = 3;
		} else {
			if (i == n)
				return arg_reject(ctx, MSG_DUR);
			u = i;
			while (i < n && !is_digit(s[i]))
				i++;
			ulen = i - u;
			for (r = 0; r < ARG__COUNTOF(dur_units); r++) {
				if (word_eq(s + u, ulen, dur_units[r].unit))
					break;
			}
			if (r == ARG__COUNTOF(dur_units))
				return arg_reject(ctx, MSG_DUR);
			if (r < next_rank)
				return arg_reject(ctx,
						  "units must be in order "
						  "d, h, m, s, ms, each once");
		}
		next_rank = r + 1;
		if (ovf || v > UINT64_MAX / dur_units[r].ms) {
			ovf = true;
			continue;
		}
		part = v * dur_units[r].ms;
		if (total > UINT64_MAX - part)
			ovf = true;
		else
			total += part;
	}
	if (ovf)
		return arg_reject_status(ctx, ARG_E_RANGE, MSG_RANGE);
	memcpy(dst, &total, sizeof(total));
	return 0;
}

static int duration_format(const arg_type *t, const void *src, char *buf,
			   size_t cap)
{
	struct sbuf b = { buf, cap, 0 };
	uint64_t v;
	size_t r;

	(void)t;
	memcpy(&v, src, sizeof(v));
	if (v == 0) {
		sb_puts(&b, "0s");
		return sb_end(&b);
	}
	for (r = 0; r < ARG__COUNTOF(dur_units); r++) {
		uint64_t q = v / dur_units[r].ms;

		if (q) {
			sb_putu(&b, q);
			sb_puts(&b, dur_units[r].unit);
			v -= q * dur_units[r].ms;
		}
	}
	return sb_end(&b);
}

const arg_type arg_t_duration = {
	.name = "DURATION",
	.size = sizeof(uint64_t),
	.parse = duration_parse,
	.format = duration_format,
	.kind = ARG_K_UINT,
};

/* ------------------------------------------------------------------ */
/* Decorators (SDD 8.3)                                               */
/* ------------------------------------------------------------------ */

#define MSG_BAD_TYPE "invalid type"

int arg_range_parse(const arg_type *t, const char *s, size_t n, void *dst,
		    arg_ctx *ctx)
{
	const arg_range *r = t->ud;
	union {
		int64_t i;
		uint64_t u;
		double d;
		unsigned char b[16];
	} tmp;
	bool ok, in;

	if (!r || !r->inner || !r->inner->parse ||
	    r->inner->size > sizeof(tmp))
		return arg_reject(ctx, MSG_BAD_TYPE);
	memset(&tmp, 0, sizeof(tmp));
	if (r->inner->parse(r->inner, s, n, &tmp, ctx) != 0)
		return -1;
	switch (t->kind) {
	case ARG_K_INT: {
		int64_t v;

		ok = arg__load_int(r->inner, &tmp, &v);
		in = ok && v >= r->lo.i && v <= r->hi.i;
		break;
	}
	case ARG_K_UINT: {
		uint64_t v;

		ok = arg__load_uint(r->inner, &tmp, &v);
		in = ok && v >= r->lo.u && v <= r->hi.u;
		break;
	}
	case ARG_K_DOUBLE: {
		double v;

		ok = arg__load_double(r->inner, &tmp, &v);
		in = ok && v >= r->lo.d && v <= r->hi.d;
		break;
	}
	default:
		ok = in = false;
		break;
	}
	if (!ok)
		return arg_reject(ctx, MSG_BAD_TYPE);
	if (!in)
		return arg_reject_status(ctx, ARG_E_RANGE, MSG_RANGE);
	memcpy(dst, &tmp, r->inner->size);
	return 0;
}

int arg_nonempty_parse(const arg_type *t, const char *s, size_t n, void *dst,
		       arg_ctx *ctx)
{
	const arg_wrap *w = t->ud;

	if (!w || !w->inner || !w->inner->parse)
		return arg_reject(ctx, MSG_BAD_TYPE);
	if (n == 0)
		return arg_reject(ctx, "must not be empty");
	return w->inner->parse(w->inner, s, n, dst, ctx);
}

int arg_maxlen_parse(const arg_type *t, const char *s, size_t n, void *dst,
		     arg_ctx *ctx)
{
	const arg_wrap *w = t->ud;

	if (!w || !w->inner || !w->inner->parse)
		return arg_reject(ctx, MSG_BAD_TYPE);
	if (n > w->n)
		return arg_reject(ctx, "too long");
	return w->inner->parse(w->inner, s, n, dst, ctx);
}

/* Stores s itself; the caller guarantees s[n] == '\0'. */
int arg_one_of_parse(const arg_type *t, const char *s, size_t n, void *dst,
		     arg_ctx *ctx)
{
	const char *const *words = t->ud;
	size_t i;

	if (!words)
		return arg_reject(ctx, MSG_BAD_TYPE);
	for (i = 0; words[i]; i++) {
		if (word_eq(s, n, words[i])) {
			memcpy(dst, &s, sizeof(s));
			return 0;
		}
	}
	return arg_reject(ctx, "invalid choice");
}

int arg_wrap_format(const arg_type *t, const void *src, char *buf,
		    size_t cap)
{
	const arg_type *inner;

	if (!t || !t->ud)
		return -1;
	/* arg_range and arg_wrap both start with the inner type */
	inner = *(const arg_type *const *)t->ud;
	if (!inner || !inner->format)
		return -1;
	return inner->format(inner, src, buf, cap);
}
