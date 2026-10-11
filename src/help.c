// SPDX-License-Identifier: GPL-2.0-only
/*
 * help.c - help and usage output (SDD 11.1)
 *
 * stub, replaced by issue #8: prints only the usage line.
 */
#include "arg_internal.h"

int arg__render_help(arg *p, const arg_item *cmd, arg_write_fn w, void *ud)
{
	struct arg__pathbuf pb;
	static const char tail[] = " [OPTIONS]\n";

	if (!arg__cmd_path(p, cmd, &pb))
		return -1;
	if (w(ud, "Usage: ", 7) || arg__write_path(w, ud, &pb.path))
		return -1;
	return w(ud, tail, sizeof(tail) - 1) ? -1 : 0;
}

int arg_help(arg *p, const char *path, arg_write_fn write, void *ud)
{
	const struct arg__priv *v = arg__priv(p);
	struct arg__pathbuf pb;
	const arg_item *cmd = arg__find_cmd(p, path, &pb);

	if (!cmd)
		return -1;
	if (!write) {
		write = v->cfg.write_out;
		ud = v->cfg.io_ud;
	}
	if (v->cfg.render_help)
		return v->cfg.render_help(p, cmd, write, ud);
	return arg__render_help(p, cmd, write, ud);
}
