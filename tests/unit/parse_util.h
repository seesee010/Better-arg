/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * parse_util.h - fixtures shared by the test_parse_*.c suites
 *
 *	struct fx f;
 *
 *	fx_init(&f, spec);
 *	CHECK_INT(PARSE(&f, "-v", "file"), ARG_OK);
 *	CHECK(arg_flag(&f.p, "verbose"));
 *
 * Output goes to f.out / f.err, the environment comes from f.env (a
 * NULL-terminated name/value table) and never from the real one.
 * Helpers are static inline so that unused ones do not warn.
 */
#ifndef ARG_PARSE_UTIL_H
#define ARG_PARSE_UTIL_H

#include "arg_internal.h"
#include "test.h"

struct fx_env {
	const char *name, *value;
};

struct fx {
	arg p;
	arg_config cfg;
	struct test_buf out, err;
	const struct fx_env *env;
	const arg_item *spec;
};

static inline int fx_out(void *ud, const char *s, size_t n)
{
	return test_buf_write(&((struct fx *)ud)->out, s, n);
}

static inline int fx_err(void *ud, const char *s, size_t n)
{
	return test_buf_write(&((struct fx *)ud)->err, s, n);
}

static inline const char *fx_getenv(void *ud, const char *name)
{
	const struct fx *f = ud;
	const struct fx_env *e;

	for (e = f->env; e && e->name; e++) {
		if (strcmp(e->name, name) == 0)
			return e->value;
	}
	return NULL;
}

/* Config with capture hooks; call fx_start() after changing f->cfg. */
static inline void fx_config(struct fx *f, const arg_item *spec)
{
	arg_config c = ARG_CONFIG_INIT;

	memset(f, 0, sizeof(*f));
	f->cfg = c;
	f->cfg.write_out = fx_out;
	f->cfg.write_err = fx_err;
	f->cfg.io_ud = f;
	f->cfg.getenv = fx_getenv;
	f->cfg.env_ud = f;
	f->spec = spec;
	test_buf_reset(&f->out);
	test_buf_reset(&f->err);
}

static inline void fx_start(struct fx *f)
{
	arg_init(&f->p, f->spec, &f->cfg);
}

static inline void fx_init(struct fx *f, const arg_item *spec)
{
	fx_config(f, spec);
	fx_start(f);
}

static inline int fx_argc(char *const *argv)
{
	int n = 0;

	while (argv[n])
		n++;
	return n;
}

static inline arg_status fx_parse(struct fx *f, char *const *argv)
{
	return arg_parse(&f->p, fx_argc(argv), argv);
}

#define PARSE(f, ...) fx_parse((f), (char *[]){ "prog", __VA_ARGS__, NULL })
#define PARSE0(f) fx_parse((f), (char *[]){ "prog", NULL })

static inline const arg_error *fx_error(struct fx *f)
{
	return arg_error_get(&f->p);
}

/* The error as printed by arg_report(). */
static inline const char *fx_report(struct fx *f)
{
	test_buf_reset(&f->err);
	test_buf_reset(&f->out);
	arg_report(&f->p);
	return f->err.data;
}

#endif /* ARG_PARSE_UTIL_H */
