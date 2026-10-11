// SPDX-License-Identifier: GPL-2.0-only
/*
 * test_help.c - generated help, usage and version output (SDD 11)
 *
 * Every layout test compares the complete output with a golden string,
 * so any change of a single byte shows up here.
 */
#include "parse_util.h"

static int ok_run(arg *a)
{
	(void)a;
	return 0;
}

static int on_plugin(arg *a, const char *v)
{
	(void)a;
	(void)v;
	return 0;
}

static int do_list(arg *a)
{
	(void)a;
	return 0;
}

/* Golden check that prints both texts in full on a mismatch. */
#define CHECK_HELP(got, want)                                                \
	do {                                                                 \
		const char *g_ = (got), *w_ = (want);                        \
		t_->checks++;                                                \
		if (strcmp(g_, w_) != 0)                                     \
			TEST_FAIL_("help differs\n--- got ---\n%s"           \
				   "--- want ---\n%s---",                     \
				   g_, w_);                                   \
	} while (0)

/* Help for path through arg_help() into f->out. */
static const char *help_of(struct fx *f, const char *path)
{
	test_buf_reset(&f->out);
	if (arg_help(&f->p, path, NULL, NULL) != 0)
		return "(arg_help failed)\n";
	return f->out.data;
}

/* What arg_report() prints for argv. */
#define REPORT(f, ...)                                                       \
	(PARSE(f, __VA_ARGS__), test_buf_reset(&(f)->out),                   \
	 arg_report(&(f)->p), (f)->out.data)

/* ------------------------------------------------------------------ */
/* SDD 11.1                                                           */
/* ------------------------------------------------------------------ */

static const arg_item mytool[] = { ARG_APP(
	"mytool", "Compiles things quickly.",
	ARG_COUNT('v', "verbose", "More output (repeatable)"),
	ARG_STR('o', "output", "Output file", .value = "FILE",
		.def = "out.bin"),
	ARG_INT('j', "jobs", "Parallel jobs", .min = "1", .max = "256",
		.def = "4", .env = "MYTOOL_JOBS"),
	ARG_ENUM(0, "format", "Output format", ARG_CHOICES("bin", "hex", "elf"),
		 .value = "FORMAT", .def = "bin"),
	ARG_ARGS("INPUT", "Input files"),
	ARG_CMD("build", ok_run, "Build the project"),
	ARG_CMD("clean", ok_run, "Remove build output")) };

static const char mytool_help[] =
	"Usage: mytool [OPTIONS] <INPUT>... <COMMAND>\n"
	"\n"
	"Compiles things quickly.\n"
	"\n"
	"Arguments:\n"
	"  <INPUT>...              Input files\n"
	"\n"
	"Options:\n"
	"  -v, --verbose           More output (repeatable)\n"
	"  -o, --output <FILE>     Output file [default: out.bin]\n"
	"  -j, --jobs <N>          Parallel jobs (1-256) [default: 4] "
	"[env: MYTOOL_JOBS]\n"
	"      --format <FORMAT>   Output format [default: bin]\n"
	"                          [possible: bin, hex, elf]\n"
	"  -h, --help              Print help\n"
	"  -V, --version           Print version\n"
	"\n"
	"Commands:\n"
	"  build    Build the project\n"
	"  clean    Remove build output\n"
	"\n"
	"See 'mytool <command> --help' for more.\n";

static void mytool_init(struct fx *f)
{
	fx_config(f, mytool);
	f->cfg.version_text = "mytool 1.0";
	fx_start(f);
}

TEST(sdd_example)
{
	struct fx f;

	CHECK_INT(arg_compile(mytool, NULL, NULL), ARG_OK);
	mytool_init(&f);
	CHECK_HELP(help_of(&f, NULL), mytool_help);
	CHECK_HELP(help_of(&f, ""), mytool_help);
}

TEST(sdd_example_via_report)
{
	struct fx f;
	int code;

	mytool_init(&f);
	f.cfg.version_text = "mytool 1.0";
	CHECK_INT(PARSE(&f, "--help"), ARG_EXIT_HELP);
	test_buf_reset(&f.out);
	code = arg_report(&f.p);
	CHECK_INT(code, 0);
	/* argv[0] of PARSE() is "prog" */
	CHECK(strncmp(f.out.data, "Usage: prog [OPTIONS]", 21) == 0);
	CHECK(strstr(f.out.data, "See 'prog <command> --help' for more.\n"));
	CHECK_STR(f.err.data, "");
}

TEST(version_output)
{
	struct fx f;

	mytool_init(&f);
	CHECK_STR(REPORT(&f, "-V"), "mytool 1.0\n");
	mytool_init(&f);
	CHECK_STR(REPORT(&f, "--version"), "mytool 1.0\n");
	CHECK_INT(arg_exit_code(ARG_EXIT_VERSION), 0);
}

/* ------------------------------------------------------------------ */
/* Subcommands and globals                                            */
/* ------------------------------------------------------------------ */

ARG_DEFINE(repo_tree,
	   ARG_CMD("repo", NULL, "Manage repositories",
		   ARG_FLAG('n', "dry-run", "Only show what would happen"),
		   ARG_CMD("add", ok_run, "Add a repository",
			   ARG_ARG("name", "Repository name"),
			   ARG_ARG_OPT("url", "Repository URL"),
			   ARG_FLAG(0, "trusted", "Skip checks")),
		   ARG_CMD("list", ok_run, "List repositories",
			   ARG_ALIAS("ls"))));
ARG_EXTERN(repo_tree);

static const arg_item pkg[] = { ARG_APP(
	"pkg", "A tiny package manager.",
	ARG_FLAG('v', "verbose", "Print more", .global = 1),
	ARG_STR('r', "root", "Install prefix", .def = "/"),
	ARG_STR(0, "secret", "Hidden global", .global = 1, .hidden = 1),
	ARG_USE(repo_tree),
	ARG_CMD("install", ok_run, "Install packages", ARG_ALIAS("i"),
		ARG_ARGS_OPT("pkg", "Packages"))) };

#define REPO_ADD_BODY                                                        \
	"\n" \
	"Add a repository\n" \
	"\n" \
	"Arguments:\n" \
	"  <name>          Repository name\n" \
	"  [url]           Repository URL\n" \
	"\n" \
	"Options:\n" \
	"      --trusted   Skip checks\n" \
	"  -v, --verbose   Print more\n" \
	"  -h, --help      Print help\n"

static const char repo_add_help[] =
	"Usage: pkg repo add [OPTIONS] <name> [url]\n" REPO_ADD_BODY;
static const char repo_add_prog[] =
	"Usage: prog repo add [OPTIONS] <name> [url]\n" REPO_ADD_BODY;

TEST(nested_with_globals)
{
	struct fx f;

	CHECK_INT(arg_compile(pkg, NULL, NULL), ARG_OK);
	fx_init(&f, pkg);
	CHECK_HELP(help_of(&f, "repo add"), repo_add_help);
	CHECK_HELP(help_of(&f, "  repo   add "), repo_add_help);
	CHECK_HELP(help_of(&f, "repo"),
		   "Usage: pkg repo [OPTIONS] <COMMAND>\n"
		   "\n"
		   "Manage repositories\n"
		   "\n"
		   "Options:\n"
		   "  -n, --dry-run   Only show what would happen\n"
		   "  -v, --verbose   Print more\n"
		   "  -h, --help      Print help\n"
		   "\n"
		   "Commands:\n"
		   "  add     Add a repository\n"
		   "  list    List repositories\n"
		   "\n"
		   "See 'pkg repo <command> --help' for more.\n");
	CHECK_HELP(help_of(&f, "i"),
		   "Usage: pkg install [OPTIONS] [pkg]...\n"
		   "\n"
		   "Install packages\n"
		   "\n"
		   "Arguments:\n"
		   "  [pkg]...        Packages\n"
		   "\n"
		   "Options:\n"
		   "  -v, --verbose   Print more\n"
		   "  -h, --help      Print help\n");
}

TEST(help_flag_in_subcommand)
{
	struct fx f;
	const char *out;

	fx_init(&f, pkg);
	out = REPORT(&f, "repo", "add", "-h");
	CHECK_INT(arg_error_get(&f.p)->status, ARG_EXIT_HELP);
	CHECK_HELP(out, repo_add_prog);
	/* the program name comes from argv[0] */
	fx_init(&f, pkg);
	CHECK_INT(fx_parse(&f, (char *[]){ "/usr/bin/pk\x01g", "repo", "add",
					   "--help", NULL }),
		  ARG_EXIT_HELP);
	test_buf_reset(&f.out);
	CHECK_INT(arg_report(&f.p), 0);
	CHECK(strncmp(f.out.data, "Usage: pk\\x01g repo add [OPTIONS]", 33) ==
	      0);
	/* arg_help(NULL) follows the selected command */
	CHECK(strncmp(help_of(&f, NULL), "Usage: pk\\x01g repo add ", 24) == 0);
}

TEST(help_command)
{
	struct fx f;

	fx_config(&f, pkg);
	f.cfg.style |= ARG_HELP_COMMAND;
	fx_start(&f);
	CHECK_HELP(REPORT(&f, "help", "repo", "add"), repo_add_prog);
	fx_config(&f, pkg);
	f.cfg.style |= ARG_HELP_COMMAND;
	fx_start(&f);
	CHECK(strncmp(REPORT(&f, "help", "repo"),
		      "Usage: prog repo [OPTIONS] <COMMAND>\n", 37) == 0);
	fx_config(&f, pkg);
	f.cfg.style |= ARG_HELP_COMMAND;
	fx_start(&f);
	CHECK(strncmp(REPORT(&f, "help"), "Usage: prog [OPTIONS] <COMMAND>\n",
		      32) == 0);
}

TEST(help_paths)
{
	struct fx f;
	struct test_buf b;

	fx_init(&f, pkg);
	CHECK_INT(arg_help(&f.p, "nope", NULL, NULL), -1);
	CHECK_INT(arg_help(&f.p, "repo nope", NULL, NULL), -1);
	CHECK_INT(arg_help(&f.p, "repo add extra", NULL, NULL), -1);
	CHECK_STR(f.out.data, "");
	/* explicit writer instead of cfg.write_out */
	test_buf_reset(&b);
	CHECK_INT(arg_help(&f.p, "repo add", test_buf_write, &b), 0);
	CHECK_HELP(b.data, repo_add_help);
	CHECK_STR(f.out.data, "");
	/* aliases name commands too */
	test_buf_reset(&b);
	CHECK_INT(arg_help(&f.p, "repo ls", test_buf_write, &b), 0);
	CHECK(strncmp(b.data, "Usage: pkg repo list [OPTIONS]\n", 31) == 0);
	/* the built-in renderer rejects foreign commands and writers */
	CHECK_INT(arg__render_help(&f.p, NULL, test_buf_write, &b), -1);
	CHECK_INT(arg__render_help(&f.p, mytool, test_buf_write, &b), -1);
	CHECK_INT(arg__render_help(&f.p, pkg, NULL, NULL), -1);
}

/* ------------------------------------------------------------------ */
/* Sections                                                           */
/* ------------------------------------------------------------------ */

static const arg_item grouped[] = { ARG_APP(
	"net", "Network tool.",
	ARG_FLAG('4', "ipv4", "Use IPv4"),
	ARG_GROUP("Connection"),
	ARG_STR('H', "host", "Host name", .value = "HOST"),
	ARG_UINT('p', "port", "Port", .min = "1", .max = "65535"),
	ARG_GROUP("Empty"),
	ARG_FLAG(0, "ghost", "Not listed", .hidden = 1),
	ARG_GROUP("Output"),
	ARG_COUNT('q', "quiet", "Less output"),
	ARG_MUTEX("ipv4", "quiet"),
	ARG_EXAMPLE("net -H example.org", "Connect to example.org"),
	ARG_EXAMPLE("net -q -p 8080 --host localhost",
		    "Quietly connect to the local machine on a port that "
		    "needs no root rights"),
	ARG_EXAMPLE("net", NULL),
	ARG_MAIN(ok_run)) };

TEST(groups_and_examples)
{
	struct fx f;

	CHECK_INT(arg_compile(grouped, NULL, NULL), ARG_OK);
	fx_init(&f, grouped);
	CHECK_HELP(help_of(&f, NULL),
		   "Usage: net [OPTIONS]\n"
		   "\n"
		   "Network tool.\n"
		   "\n"
		   "Options:\n"
		   "  -4, --ipv4          Use IPv4\n"
		   "  -h, --help          Print help\n"
		   "\n"
		   "Connection:\n"
		   "  -H, --host <HOST>   Host name\n"
		   "  -p, --port <N>      Port (1-65535)\n"
		   "\n"
		   "Output:\n"
		   "  -q, --quiet         Less output\n"
		   "\n"
		   "Examples:\n"
		   "  net -H example.org\n"
		   "    Connect to example.org\n"
		   "  net -q -p 8080 --host localhost\n"
		   "    Quietly connect to the local machine on a port that "
		   "needs no root rights\n"
		   "  net\n");
}

static const arg_item only_groups[] = { ARG_APP(
	"g", NULL,
	ARG_GROUP("Main"),
	ARG_FLAG('a', "all", "Everything")) };

TEST(no_ungrouped_options)
{
	struct fx f;

	fx_config(&f, only_groups);
	f.cfg.style |= ARG_NO_AUTO_HELP;
	fx_start(&f);
	CHECK_HELP(help_of(&f, NULL),
		   "Usage: g [OPTIONS]\n"
		   "\n"
		   "Main:\n"
		   "  -a, --all   Everything\n");
}

static const arg_item hidden[] = { ARG_APP(
	"h", "Hidden things.",
	ARG_FLAG('x', NULL, "Shown"),
	ARG_FLAG('y', "secret", "Not shown", .hidden = 1),
	ARG_ARG_OPT("file", "Shown file"),
	ARG_ARG_OPT("extra", "Not shown", .hidden = 1),
	{ .kind = ARG_I_CMD, .name = "debug", .fn.run = ok_run,
	  .help = "Not shown", .hidden = 1, .items = ARG__KIDS(0) }) };

TEST(hidden_items)
{
	struct fx f;

	CHECK_INT(arg_compile(hidden, NULL, NULL), ARG_OK);
	fx_init(&f, hidden);
	CHECK_HELP(help_of(&f, NULL),
		   "Usage: h [OPTIONS] [file]\n"
		   "\n"
		   "Hidden things.\n"
		   "\n"
		   "Arguments:\n"
		   "  [file]       Shown file\n"
		   "\n"
		   "Options:\n"
		   "  -x           Shown\n"
		   "  -h, --help   Print help\n");
}

static const arg_item kinds[] = { ARG_APP(
	"k", "Option kinds.",
	ARG_FLAG(0, "color", "Colour output", .negatable = 1),
	ARG_STR('o', "output", "Output file", .alias = "out"),
	ARG_STR(0, "level", "Level", .optional_value = 1, .value = "L"),
	ARG_ON('P', "plugin", on_plugin, "Load a plugin", .value = "SO"),
	ARG_ON(0, "tick", on_plugin, "Count ticks"),
	ARG_DO('l', "list", do_list, "List formats"),
	ARG_CUSTOM('m', "mode", ARG_ONE_OF_STR("fast", "safe"), "Mode"),
	ARG_SIZE(0, "cache", "Cache size", .min = "4k"),
	ARG_DURATION(0, "timeout", "Timeout", .max = "1m"),
	ARG_ARG("SRC", "Source", .env = "K_SRC", .def = "a.c"),
	ARG_ARGS_OPT("DST", "Destinations")) };

TEST(option_forms)
{
	struct fx f;

	CHECK_INT(arg_compile(kinds, NULL, NULL), ARG_OK);
	fx_init(&f, kinds);
	CHECK_HELP(help_of(&f, NULL),
		   "Usage: k [OPTIONS] <SRC> [DST]...\n"
		   "\n"
		   "Option kinds.\n"
		   "\n"
		   "Arguments:\n"
		   "  <SRC>                        Source [default: a.c] "
		   "[env: K_SRC]\n"
		   "  [DST]...                     Destinations\n"
		   "\n"
		   "Options:\n"
		   "      --[no-]color             Colour output\n"
		   "  -o, --output, --out <TEXT>   Output file\n"
		   "      --level[=<L>]            Level\n"
		   "  -P, --plugin <SO>            Load a plugin\n"
		   "      --tick                   Count ticks\n"
		   "  -l, --list                   List formats\n"
		   "  -m, --mode <WORD>            Mode [possible: fast, safe]\n"
		   "      --cache <SIZE>           Cache size (>= 4k)\n"
		   "      --timeout <DURATION>     Timeout (<= 1m)\n"
		   "  -h, --help                   Print help\n");
}

/* ------------------------------------------------------------------ */
/* Wrapping and width                                                 */
/* ------------------------------------------------------------------ */

static const arg_item wrapping[] = { ARG_APP(
	"wrap",
	"A summary that is long enough to need wrapping at forty columns "
	"and at sixty.",
	ARG_INT('j', "jobs", "Number of parallel jobs to run at once",
		.min = "1", .max = "64", .def = "4", .env = "WRAP_JOBS"),
	ARG_ENUM(0, "fmt", "Format", ARG_CHOICES("a", "b"))) };

static const char wrap40[] =
	"Usage: wrap [OPTIONS]\n"
	"\n"
	"A summary that is long enough to need\n"
	"wrapping at forty columns and at sixty.\n"
	"\n"
	"Options:\n"
	"  -j, --jobs <N>    Number of parallel\n"
	"                    jobs to run at once\n"
	"                    (1-64) [default: 4]\n"
	"                    [env: WRAP_JOBS]\n"
	"      --fmt <CHOICE>\n"
	"                    Format\n"
	"                    [possible: a, b]\n"
	"  -h, --help        Print help\n";

static const char wrap60[] =
	"Usage: wrap [OPTIONS]\n"
	"\n"
	"A summary that is long enough to need wrapping at forty\n"
	"columns and at sixty.\n"
	"\n"
	"Options:\n"
	"  -j, --jobs <N>       Number of parallel jobs to run at\n"
	"                       once (1-64) [default: 4]\n"
	"                       [env: WRAP_JOBS]\n"
	"      --fmt <CHOICE>   Format [possible: a, b]\n"
	"  -h, --help           Print help\n";

TEST(wrap_width_40_and_60)
{
	struct fx f;

	fx_config(&f, wrapping);
	f.cfg.help_width = 40;
	fx_start(&f);
	CHECK_HELP(help_of(&f, NULL), wrap40);
	fx_config(&f, wrapping);
	f.cfg.help_width = 60;
	fx_start(&f);
	CHECK_HELP(help_of(&f, NULL), wrap60);
}

static unsigned width_hook(void *ud)
{
	return *(const unsigned *)ud;
}

TEST(width_sources)
{
	static const struct fx_env cols40[] = { { "COLUMNS", "40" },
						{ NULL, NULL } };
	static const struct fx_env cols60[] = { { "COLUMNS", "60" },
						{ NULL, NULL } };
	static const char *const bad[] = { "", "abc", "19", "1001", "4x0",
					   "99999999999999999999", "-60" };
	unsigned w40 = 40, w0 = 0;
	struct fx f;
	size_t i;

	/* COLUMNS through the getenv hook */
	fx_config(&f, wrapping);
	f.env = cols40;
	fx_start(&f);
	CHECK_HELP(help_of(&f, NULL), wrap40);
	/* term_width hook wins over COLUMNS */
	fx_config(&f, wrapping);
	f.env = cols60;
	f.cfg.term_width = width_hook;
	f.cfg.width_ud = &w40;
	fx_start(&f);
	CHECK_HELP(help_of(&f, NULL), wrap40);
	/* term_width 0 = unknown, falls back to COLUMNS */
	f.cfg.width_ud = &w0;
	fx_start(&f);
	CHECK_HELP(help_of(&f, NULL), wrap60);
	/* help_width wins over both */
	f.cfg.width_ud = &w40;
	f.cfg.help_width = 60;
	fx_start(&f);
	CHECK_HELP(help_of(&f, NULL), wrap60);
	/* COLUMNS that is not a sane number is ignored: 80 */
	for (i = 0; i < ARG__COUNTOF(bad); i++) {
		struct fx_env env[] = { { "COLUMNS", bad[i] }, { NULL, NULL } };

		fx_config(&f, wrapping);
		f.env = env;
		fx_start(&f);
		CHECK(strstr(help_of(&f, NULL),
			     "wrapping at forty columns and at sixty.\n"));
	}
	/* no getenv hook at all */
	fx_init(&f, wrapping);
	arg__priv(&f.p)->cfg.getenv = NULL;
	CHECK(strstr(help_of(&f, NULL), "Format [possible: a, b]\n"));
}

static const arg_item long_cells[] = { ARG_APP(
	"lc", NULL,
	ARG_STR('s', "short", "Short one"),
	ARG_STR(0, "a-really-long-option-name", "Its description",
		.value = "SOMETHING"),
	ARG_FLAG('n', NULL, NULL),
	ARG_CMD("a-very-long-command-name-indeed", ok_run, "Long command"),
	ARG_CMD("x", ok_run, NULL),
	ARG_MAIN(ok_run)) };

TEST(long_left_cells)
{
	struct fx f;

	fx_init(&f, long_cells);
	CHECK_HELP(help_of(&f, NULL),
		   "Usage: lc [OPTIONS] [COMMAND]\n"
		   "\n"
		   "Options:\n"
		   "  -s, --short <TEXT>                    Short one\n"
		   "      --a-really-long-option-name <SOMETHING>\n"
		   "                                        Its description\n"
		   "  -n\n"
		   "  -h, --help                            Print help\n"
		   "\n"
		   "Commands:\n"
		   "  a-very-long-command-name-indeed    Long command\n"
		   "  x\n"
		   "\n"
		   "See 'lc <command> --help' for more.\n");
}

static const arg_item utf8[] = { ARG_APP(
	"u", "Ünïcödé wörds ärë cöüntëd äs öne cölümn ëäch, nöt äs bytës.",
	ARG_FLAG('g', "grüße", "Grüße an älle Lëütë öhne Ümlaut-Fëhler")) };

TEST(utf8_width)
{
	struct fx f;

	fx_config(&f, utf8);
	f.cfg.help_width = 40;
	fx_start(&f);
	CHECK_HELP(help_of(&f, NULL),
		   "Usage: u [OPTIONS]\n"
		   "\n"
		   "Ünïcödé wörds ärë cöüntëd äs öne cölümn\n"
		   "ëäch, nöt äs bytës.\n"
		   "\n"
		   "Options:\n"
		   "  -g, --grüße   Grüße an älle Lëütë\n"
		   "                öhne Ümlaut-Fëhler\n"
		   "  -h, --help    Print help\n");
}

/* 150 bytes: longer than the word buffer and the line */
#define LONG_WORD                                                            \
	"0123456789012345678901234567890123456789012345678901234567890123456" \
	"7890123456789012345678901234567890123456789012345678901234567890123" \
	"4567890123456789"

static const arg_item odd_text[] = { ARG_APP(
	"o",
	"First paragraph.\nSecond  paragraph   with  spaces.\n\n" LONG_WORD
	" end",
	ARG_FLAG('a', NULL, "Text with a\nbreak")) };

TEST(newlines_and_long_words)
{
	struct fx f;

	fx_config(&f, odd_text);
	f.cfg.help_width = 40;
	fx_start(&f);
	CHECK_HELP(help_of(&f, NULL),
		   "Usage: o [OPTIONS]\n"
		   "\n"
		   "First paragraph.\n"
		   "Second paragraph with spaces.\n"
		   "\n"
		   LONG_WORD "\n"
		   "end\n"
		   "\n"
		   "Options:\n"
		   "  -a           Text with a\n"
		   "               break\n"
		   "  -h, --help   Print help\n");
}

TEST(tiny_width)
{
	struct fx f;

	fx_config(&f, wrapping);
	f.cfg.help_width = 1;
	fx_start(&f);
	CHECK(strstr(help_of(&f, NULL), "Options:\n  -j, --jobs <N>\nNumber\n"
					 "of\nparallel\n"));
}

/* ------------------------------------------------------------------ */
/* Automatic rows                                                     */
/* ------------------------------------------------------------------ */

static const arg_item plain[] = { ARG_APP(
	"plain", "No options.", ARG_ARG("x", "Thing")) };

TEST(no_auto_help)
{
	struct fx f;

	fx_config(&f, plain);
	f.cfg.style |= ARG_NO_AUTO_HELP;
	fx_start(&f);
	CHECK_HELP(help_of(&f, NULL),
		   "Usage: plain <x>\n"
		   "\n"
		   "No options.\n"
		   "\n"
		   "Arguments:\n"
		   "  <x>   Thing\n");
	/* only the version row is left */
	f.cfg.version_text = "1";
	fx_start(&f);
	CHECK_HELP(help_of(&f, NULL),
		   "Usage: plain [OPTIONS] <x>\n"
		   "\n"
		   "No options.\n"
		   "\n"
		   "Arguments:\n"
		   "  <x>             Thing\n"
		   "\n"
		   "Options:\n"
		   "  -V, --version   Print version\n");
	/* -h is then an unknown option */
	fx_start(&f);
	CHECK_INT(PARSE(&f, "-h"), ARG_E_UNKNOWN_OPTION);
}

static const arg_item claims[] = { ARG_APP(
	"c", NULL,
	ARG_FLAG('h', "host-only", "Own -h"),
	ARG_FLAG('V', NULL, "Own -V", .global = 1),
	ARG_FLAG(0, "ver", "Own --version", .alias = "version"),
	ARG_CMD("sub", ok_run, "Sub",
		ARG_FLAG(0, "x", "X", .alias = "help"),
		ARG_FLAG('h', NULL, "Own -h too", .hidden = 1))) };

TEST(user_claims_auto_names)
{
	struct fx f;

	CHECK_INT(arg_compile(claims, NULL, NULL), ARG_OK);
	fx_config(&f, claims);
	f.cfg.version_text = "c 1";
	fx_start(&f);
	CHECK_HELP(help_of(&f, NULL),
		   "Usage: c [OPTIONS] <COMMAND>\n"
		   "\n"
		   "Options:\n"
		   "  -h, --host-only        Own -h\n"
		   "  -V                     Own -V\n"
		   "      --ver, --version   Own --version\n"
		   "      --help             Print help\n"
		   "\n"
		   "Commands:\n"
		   "  sub    Sub\n"
		   "\n"
		   "See 'c <command> --help' for more.\n");
	/* -V is reached as a global, --version is not */
	CHECK_HELP(help_of(&f, "sub"),
		   "Usage: c sub [OPTIONS]\n"
		   "\n"
		   "Sub\n"
		   "\n"
		   "Options:\n"
		   "      --x, --help   X\n"
		   "  -V                Own -V\n"
		   "      --version     Print version\n");
	/* the parser agrees: --help is the user's, --version is automatic */
	fx_start(&f);
	CHECK_INT(PARSE(&f, "sub", "--help"), ARG_OK);
	fx_start(&f);
	CHECK_INT(PARSE(&f, "sub", "--version"), ARG_EXIT_VERSION);
	fx_start(&f);
	CHECK_INT(PARSE(&f, "--help"), ARG_EXIT_HELP);
	fx_start(&f);
	CHECK_INT(PARSE(&f, "-V", "sub"), ARG_OK);
}

/* ------------------------------------------------------------------ */
/* Hooks                                                              */
/* ------------------------------------------------------------------ */

struct rh {
	int calls;
	const arg_item *cmd;
};

static int my_render(arg *a, const arg_item *cmd, arg_write_fn w, void *ud)
{
	struct rh *r = arg_udata(a);

	r->calls++;
	r->cmd = cmd;
	return w(ud, "custom\n", 7) ? -1 : 5;
}

TEST(render_help_hook)
{
	struct rh r = { 0, NULL };
	struct fx f;

	fx_config(&f, pkg);
	f.cfg.render_help = my_render;
	f.cfg.udata = &r;
	fx_start(&f);
	CHECK_STR(REPORT(&f, "repo", "--help"), "custom\n");
	CHECK_INT(r.calls, 1);
	REQUIRE(r.cmd != NULL);
	CHECK_STR(r.cmd->name, "repo");
	/* arg_help passes the hook's result through */
	test_buf_reset(&f.out);
	CHECK_INT(arg_help(&f.p, "install", NULL, NULL), 5);
	CHECK_STR(f.out.data, "custom\n");
	CHECK_INT(r.calls, 2);
	CHECK_STR(r.cmd->name, "install");
	CHECK_INT(arg_help(&f.p, "missing", NULL, NULL), -1);
	CHECK_INT(r.calls, 2);
}

static const arg_item msg_spec[] = { ARG_APP(
	"t", NULL,
	ARG_ENUM('m', "mode", "Modus", ARG_CHOICES("a", "b"), .def = "a",
		 .env = "T_MODE"),
	ARG_ARG("x", "Ding"),
	ARG_CMD("run", ok_run, "Laufen"),
	ARG_EXAMPLE("t x run", "Beispiel")) };

TEST(custom_messages)
{
	static const arg_messages de = { {
		[ARG_MSG_USAGE] = "Aufruf:",
		[ARG_MSG_ARGUMENTS] = "Argumente:",
		[ARG_MSG_OPTIONS] = "Optionen:",
		[ARG_MSG_COMMANDS] = "Befehle:",
		[ARG_MSG_EXAMPLES] = "Beispiele:",
		[ARG_MSG_HELP_FLAG] = "Hilfe zeigen",
		[ARG_MSG_VERSION_FLAG] = "Version zeigen",
		[ARG_MSG_DEFAULT] = "Standard",
		[ARG_MSG_ENV] = "Umgebung",
		[ARG_MSG_POSSIBLE] = "möglich",
		[ARG_MSG_SEE_COMMAND_HELP] = "100%% mehr: '%s <Befehl> --help'",
	} };
	struct fx f;

	fx_config(&f, msg_spec);
	f.cfg.messages = &de;
	f.cfg.version_text = "t 1";
	fx_start(&f);
	CHECK_HELP(help_of(&f, NULL),
		   "Aufruf: t [OPTIONS] <x> <COMMAND>\n"
		   "\n"
		   "Argumente:\n"
		   "  <x>                   Ding\n"
		   "\n"
		   "Optionen:\n"
		   "  -m, --mode <CHOICE>   Modus [Standard: a] [Umgebung: T_MODE] "
		   "[möglich: a, b]\n"
		   "  -h, --help            Hilfe zeigen\n"
		   "  -V, --version         Version zeigen\n"
		   "\n"
		   "Befehle:\n"
		   "  run    Laufen\n"
		   "\n"
		   "Beispiele:\n"
		   "  t x run\n"
		   "    Beispiel\n"
		   "\n"
		   "100% mehr: 't <Befehl> --help'\n");
}

/* ------------------------------------------------------------------ */
/* Output stream                                                      */
/* ------------------------------------------------------------------ */

struct sink {
	struct test_buf buf;
	int calls, fail_at, code;
	size_t largest;
};

static int sink_write(void *ud, const char *s, size_t n)
{
	struct sink *k = ud;

	k->calls++;
	if (n > k->largest)
		k->largest = n;
	if (k->fail_at && k->calls == k->fail_at)
		return k->code;
	return test_buf_write(&k->buf, s, n);
}

TEST(streamed_in_small_chunks)
{
	struct sink k;
	struct fx f;

	memset(&k, 0, sizeof(k));
	mytool_init(&f);
	CHECK_INT(arg_help(&f.p, NULL, sink_write, &k), 0);
	CHECK_HELP(k.buf.data, mytool_help);
	CHECK(k.largest <= 256);
	CHECK(k.calls >= (int)(sizeof(mytool_help) / 256));
	/* help bigger than many buffers */
	memset(&k, 0, sizeof(k));
	fx_init(&f, odd_text);
	CHECK_INT(arg_help(&f.p, NULL, sink_write, &k), 0);
	CHECK(k.largest <= 256);
	CHECK(strstr(k.buf.data, LONG_WORD "\nend\n"));
}

TEST(write_errors)
{
	struct sink k;
	struct fx f;
	int i, total;

	memset(&k, 0, sizeof(k));
	mytool_init(&f);
	CHECK_INT(arg_help(&f.p, NULL, sink_write, &k), 0);
	total = k.calls;
	REQUIRE(total >= 2);
	/* the first failing write's result is returned, nothing after it */
	for (i = 1; i <= total; i++) {
		memset(&k, 0, sizeof(k));
		k.fail_at = i;
		k.code = 40 + i;
		CHECK_INT(arg_help(&f.p, NULL, sink_write, &k), 40 + i);
		CHECK_INT(k.calls, i);
	}
	/* arg_report ignores the result but still stops writing */
	mytool_init(&f);
	CHECK_INT(PARSE(&f, "-h"), ARG_EXIT_HELP);
	memset(&k, 0, sizeof(k));
	k.fail_at = 1;
	k.code = -3;
	arg__priv(&f.p)->cfg.write_out = sink_write;
	arg__priv(&f.p)->cfg.io_ud = &k;
	CHECK_INT(arg_report(&f.p), 0);
	CHECK_INT(k.calls, 1);
}

static const arg_item usage_spec[] = { ARG_APP(
	"u", NULL,
	ARG_CMD("remote", ok_run, "Remotes",
		ARG_ARGS("FILES", "Files"),
		ARG_CMD("add", ok_run, "Add a remote")),
	ARG_CMD("tools", NULL, "Tools",
		{ .kind = ARG_I_CMD, .name = "dbg", .fn.run = ok_run,
		  .help = "Hidden", .hidden = 1, .items = ARG__KIDS(0) })) };

TEST(usage_forms)
{
	struct fx f;

	CHECK_INT(arg_compile(usage_spec, NULL, NULL), ARG_OK);
	fx_config(&f, usage_spec);
	f.cfg.style |= ARG_NO_AUTO_HELP;
	fx_start(&f);
	/* a handler makes the subcommand optional */
	CHECK_HELP(help_of(&f, "remote"),
		   "Usage: u remote <FILES>... [COMMAND]\n"
		   "\n"
		   "Remotes\n"
		   "\n"
		   "Arguments:\n"
		   "  <FILES>...   Files\n"
		   "\n"
		   "Commands:\n"
		   "  add    Add a remote\n"
		   "\n"
		   "See 'u remote <command> --help' for more.\n");
	/* only hidden subcommands: nothing about commands at all */
	CHECK_HELP(help_of(&f, "tools"), "Usage: u tools\n\nTools\n");
}

TEST_MAIN(TEST_CASE(sdd_example), TEST_CASE(sdd_example_via_report),
	  TEST_CASE(version_output), TEST_CASE(nested_with_globals),
	  TEST_CASE(help_flag_in_subcommand), TEST_CASE(help_command),
	  TEST_CASE(help_paths), TEST_CASE(groups_and_examples),
	  TEST_CASE(no_ungrouped_options), TEST_CASE(hidden_items),
	  TEST_CASE(option_forms), TEST_CASE(wrap_width_40_and_60),
	  TEST_CASE(width_sources), TEST_CASE(long_left_cells),
	  TEST_CASE(utf8_width), TEST_CASE(newlines_and_long_words),
	  TEST_CASE(tiny_width), TEST_CASE(no_auto_help),
	  TEST_CASE(user_claims_auto_names), TEST_CASE(render_help_hook),
	  TEST_CASE(custom_messages), TEST_CASE(streamed_in_small_chunks),
	  TEST_CASE(write_errors), TEST_CASE(usage_forms))
