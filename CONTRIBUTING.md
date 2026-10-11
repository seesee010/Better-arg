# Contributing to Better-arg

Thanks for helping! This page is the short version. The binding rules are
in the files it links to.

## Coding style

Better-arg follows the **Linux kernel coding style**, version 4.10:

- Full text: [`docs/coding-style.rst`](docs/coding-style.rst), copied
  unchanged from
  [kernel.org](https://www.kernel.org/doc/html/v4.10/process/coding-style.html)
  (source: `Documentation/process/coding-style.rst` at tag `v4.10`,
  GPL-2.0, the same license as this project).
- Tooling: [`.clang-format`](.clang-format) and
  [`.editorconfig`](.editorconfig) encode the mechanical parts.

The essentials:

| Rule | In short |
| --- | --- |
| Indentation | Tabs, 8 columns wide. No spaces for indentation. |
| Line length | 80 columns. Never break user-visible strings. |
| Braces | Opening brace on the same line, except for functions: those put it on the next line. |
| Spaces | After `if`, `for`, `while`, `switch`; not after function names; `*` binds to the name: `char *s`. |
| Naming | `lower_snake_case`. Public symbols use the `arg_` / `ARG_` prefix; private cross-file symbols use `arg__`. |
| Functions | Short, doing one thing. Use `goto` for central cleanup. |
| Comments | `/* C89 style */` only, explaining *what* and *why*, not *how*. |
| License tag | First line `// SPDX-License-Identifier: GPL-2.0-only` in `.c` files, `/* ... */` in headers, as in the kernel. |
| Macros | Multi-statement macros in `do { } while (0)`; uppercase names. |

### Deliberate deviations from the kernel style

The kernel style targets kernel code. Better-arg is a portable user-space
library, so it deviates in these places:

1. **Typedefs for public API types.** Chapter 5 discourages typedefs. The
   public API (`arg_item`, `arg_type`, `arg_parser`, `arg`, ...) uses them
   anyway because the SDD API is designed around them and because
   declaration macros must stay short. Internal code may use plain
   `struct` names.
2. **Standard C instead of kernel APIs.** `bool`/`stdint.h` types,
   `size_t`, `memcpy` etc., but no `kmalloc`, `printk`, `likely()`.
3. **C99 only.** No C11 or later features, no GNU extensions. The build
   enforces `-std=c99 -pedantic`.

## Language and warnings

- C99, nothing newer, no compiler extensions.
- Must build warning-free with
  `-Wall -Wextra -Wpedantic -Wconversion -Wshadow` on GCC and Clang
  (configure with `-DARG_WERROR=ON` to make that fatal).
- No mutable global or static state. `argv` is never modified.

## Building and testing

```sh
cmake -S . -B build -DARG_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

With sanitizers: add `-DARG_SANITIZE=ON`. With coverage: `-DARG_COVERAGE=ON`.
Mechanical style checks: `./scripts/check-style.sh`.

Every change needs unit tests in `tests/unit/test_<module>.c`. New test
files are picked up automatically.

## Workflow

1. Every change starts as a **GitHub issue**.
2. Work on a branch named `<area>/<issue>-<slug>`, for example
   `tokenizer/2-pull-tokenizer`.
3. Open a **pull request** that says `Closes #<issue>`. CI must be green
   before it is merged.

## Commit messages

Linux kernel style
([submitting patches](https://www.kernel.org/doc/html/v4.10/process/submitting-patches.html)):

```
subsystem: short imperative summary, no trailing period

Explain what the change does and why, wrapped at 72 columns. The
body may span several paragraphs.

Refs: #12
Co-Authored-By: Name <mail@example.org>
```

`subsystem` is the area being changed: `tokenizer`, `types`, `parse`,
`help`, `build`, `ci`, `docs`, ...

## AI-generated contributions

Much of this project is written with **Claude Opus 5.5** running in the
**Claude Code** harness. AI-written issues and pull requests say so in
their first line, and every such commit carries a `Co-Authored-By:`
trailer.
