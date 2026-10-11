<div align="center">

# Better-arg

**Command line parsing for C99. One declaration, one call, done.**

[![CI](https://github.com/seesee010/Better-arg/actions/workflows/ci.yml/badge.svg)](https://github.com/seesee010/Better-arg/actions/workflows/ci.yml)
![C99](https://img.shields.io/badge/C-99-00599C?style=flat&logo=c&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-3.16%2B-064F8C?style=flat&logo=cmake&logoColor=white)
![Platforms](https://img.shields.io/badge/platform-Linux%20%7C%20macOS-lightgrey?style=flat)
![Coverage](https://img.shields.io/badge/coverage-%E2%89%A595%25%20gated-brightgreen?style=flat)
[![License: GPL-2.0](https://img.shields.io/badge/license-GPL--2.0-blue?style=flat)](LICENSE)
[![Style: Linux kernel](https://img.shields.io/badge/style-Linux%20kernel-yellow?style=flat&logo=linux&logoColor=white)](docs/coding-style.rst)
![Made With:Agentic_Engineering](https://img.shields.io/badge/made%20with-Agentic_Engineering-red?style=flat)

</div>

```c
#include "arg.h"

static int shrink(arg *a)
{
	for (size_t i = 0; i < arg_count(a, "image"); i++)
		printf("%s at quality %lld\n", arg_str_at(a, "image", i),
		       (long long)arg_int(a, "quality"));
	return 0;
}

int main(int argc, char **argv)
{
	return ARG_RUN(argc, argv, "shrink", "Makes images smaller.",
		ARG_FLAG('v', "verbose", "Print what happens"),
		ARG_INT('q', "quality", "Quality", .def = "80", .min = "1", .max = "100"),
		ARG_ARGS("image", "Images to shrink"),
		ARG_MAIN(shrink));
}
```

No init, no free, no structs to wire up. You get typed values, `--help`, errors
with suggestions and the right exit codes for free:

```console
$ shrink --qualty 50 a.png
shrink: unknown option '--qualty'
  Did you mean '--quality'?
Try 'shrink --help' for more information.

$ shrink -q 101 a.png
shrink: value '101' for '--quality <N>' is out of range: must be between 1 and 100
Try 'shrink --help' for more information.
```

## ✨ Features

|  |  |
| --- | --- |
| 🧩 **Declarative** | Options, positionals, subcommands, rules and examples live in one nested declaration |
| 🔢 **Typed** | bool, 8–64 bit ints (`0x`, `0b`, `1_000`), double, enum, sizes (`16M`, `1KiB`), durations (`1m30s`), your own types |
| 🌳 **Subcommands** | Any depth, aliases, global options, `git`-style external commands, trees split across files |
| ✅ **Validation** | required, ranges, `MUTEX`, `ONE_OF`, `ANY_OF`, `REQUIRES`, `CONFLICTS`, custom checks |
| 🌱 **Fallbacks** | CLI › extra sources › environment › default |
| 📖 **Help** | Generated, wrapped, grouped, with defaults, env names and choices |
| 🐚 **Completion** | bash, zsh and fish scripts from the same spec |
| 🛡️ **Safe** | `argv` is never modified, no globals, no heap by default, input is escaped in errors |
| 🔌 **Hookable** | output, env, allocator, errors, help, suggestions, messages, exit |
| 🪶 **Small** | Pure C99, GNU/POSIX syntax, zero dependencies |

## 🚀 Getting started

**CMake, as a subdirectory or with FetchContent**

```cmake
include(FetchContent)
FetchContent_Declare(better_arg
    GIT_REPOSITORY https://github.com/seesee010/Better-arg.git
    GIT_TAG main)
FetchContent_MakeAvailable(better_arg)

target_link_libraries(my_tool PRIVATE BetterArg::arg)
```

**Installed**

```sh
cmake -S . -B build && cmake --build build && sudo cmake --install build
```

```cmake
find_package(BetterArg REQUIRED)
target_link_libraries(my_tool PRIVATE BetterArg::arg)
```

or `cc my_tool.c $(pkg-config --cflags --libs better-arg)`.

## 🌳 Subcommands

Commands are items that hold items, so the source reads like the command tree:

```c
return ARG_RUN(argc, argv, "pkg", "A tiny package manager.",
	ARG_FLAG('v', "verbose", "Print more", .global = true),
	ARG_STR(0, "root", "Install prefix", .def = "/", .env = "PKG_ROOT"),

	ARG_CMD("install", install, "Install packages",
		ARG_ALIAS("i"),
		ARG_FLAG('f', "force", "Reinstall even if present"),
		ARG_ARGS("name", "Packages to install")),

	ARG_CMD("repo", NULL, "Manage repositories",
		ARG_CMD("add", repo_add, "Add a repository",
			ARG_ARG("name", "Repository name"),
			ARG_ARG("url", "Repository URL"))));
```

```console
$ pkg --help
Usage: pkg [OPTIONS] <COMMAND>

A tiny package manager.

Options:
  -v, --verbose       Print more
      --root <TEXT>   Install prefix [default: /] [env: PKG_ROOT]
  -h, --help          Print help

Commands:
  install    Install packages
  repo       Manage repositories

See 'pkg <command> --help' for more.
```

Handlers read values by name, including global options of parent commands:
`arg_flag(a, "verbose")`, `arg_str(a, "root")`, `arg_str_at(a, "name", i)`.

## 🧱 Three levels

| Level | What you write | When |
| --- | --- | --- |
| **0** | `return ARG_RUN(argc, argv, ...)` | Almost always |
| **1** | `static const arg_item app[] = { ARG_APP(...) };` + `arg_init` / `arg_parse` / `arg_run` | Configs, hooks, specs in ROM, your own dispatch |
| **2** | `arg_tok_next()` pull tokenizer | You only want the syntax |

Every level only uses the public API of the one below it, so nothing is a dead end.

## 📚 Examples

| Example | Shows |
| --- | --- |
| [`00-minimal`](examples/00-minimal/shrink.c) | The program above |
| [`01-pkg`](examples/01-pkg) | Nested subcommands, aliases, globals, env fallback, a tree split across files |
| [`02-level1`](examples/02-level1/todo.c) | Level 1, config, `.to` bindings, dispatch with `arg_path_is` |
| [`03-tokenizer`](examples/03-tokenizer/tokens.c) | Level 2 pull tokenizer |
| [`04-custom-type`](examples/04-custom-type/paint.c) | A `#RRGGBB` type, enums and rules |
| [`05-actions`](examples/05-actions/search.c) | `ARG_ON` / `ARG_DO` flags that run functions |
| [`06-no-exit`](examples/06-no-exit/safe.c) | Handle errors yourself with `ARG_EXIT_ON_ERROR 0` |

## 🛠️ Building and testing

```sh
cmake -S . -B build -DARG_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

| Option | Effect |
| --- | --- |
| `ARG_SANITIZE` | AddressSanitizer + UndefinedBehaviorSanitizer |
| `ARG_COVERAGE` | gcov instrumentation |
| `ARG_BUILD_TESTS`, `ARG_BUILD_EXAMPLES` | on for top-level builds |

CI builds every pull request with GCC and Clang on Linux and macOS, runs the
unit and end-to-end tests under sanitizers, checks the public header against
C99/C11/C17 with `-pedantic-errors` and enforces at least 95 % line coverage.

## 📄 Documentation

- [Software Design Document](docs/SDD.md) ([PDF](docs/SDD.pdf)): architecture, full API, parsing rules
- [`include/arg.h`](include/arg.h): the public API, commented
- [Contributing](CONTRIBUTING.md): workflow, commit style and the [Linux kernel coding style](docs/coding-style.rst)

> [!NOTE]
> 🤖 Better-arg is built with **Claude Opus 5.5** in the **Claude Code** harness, together with
> [@seesee010](https://github.com/seesee010). Every issue and pull request says so, and every
> commit carries a `Co-Authored-By` trailer.

## ⚖️ License

[GPL-2.0](LICENSE)
