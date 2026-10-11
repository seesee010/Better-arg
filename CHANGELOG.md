# Changelog

All notable changes to Better-arg are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project
uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html). During
0.x a minor release may change the API; such changes are listed under
**Changed** or **Removed**.

## [Unreleased]

### Added

- `CHANGELOG.md` (#38)

## [0.1.0] - 2026-10-11

First feature-complete version: every *Must* and *Should* requirement of
the [SDD](docs/SDD.md).

### Added

- Public API in `include/arg.h` with three levels: `ARG_RUN` (Level 0),
  static `arg_item` arrays with `arg_init` / `arg_parse` / `arg_run`
  (Level 1) and the `arg_tok_next()` pull tokenizer (Level 2) (#19, #21,
  #27)
- GNU and POSIX syntax: bundled short options, `--name=value`, optional
  values, `--`, negative numbers, options after positionals (#21, #24)
- Value types: bool, signed and unsigned 8 to 64 bit integers with `0x`,
  `0b`, `0o` and `_` separators, double, string, enum, sizes (`16M`,
  `1KiB`), durations (`1m30s`), lists and custom types (#23)
- Nested subcommands with aliases, global options, external commands and
  trees split across files (#24, #27)
- Value fallbacks: command line, extra sources, environment, default (#24)
- Rules: required, ranges, `MUTEX`, `ONE_OF`, `ANY_OF`, `REQUIRES`,
  `CONFLICTS` and custom checks (#25)
- `ARG_DO` and `ARG_ON` actions (#24)
- Errors with status codes, exit codes (2 for usage errors, `sysexits`
  values otherwise), escaped input
  and "did you mean" suggestions (#20, #22)
- Generated `--help`, usage and `--version` output (#28)
- bash, zsh and fish completion scripts (#31)
- Hooks for output, environment, allocation, errors, help, suggestions,
  messages and exit; `ARG_EXIT_ON_ERROR` to handle errors yourself (#19,
  #22, #30)
- CMake package, `pkg-config` file, examples `00-minimal` to `06-no-exit`
  (#19, #27)
- CI on Linux and macOS with GCC and Clang, sanitizers, strict header
  checks, style check, real-shell completion tests and a coverage gate
  (#19, #31)

### Fixed

- A grouping command called without a subcommand prints its help and
  exits with 2 (#30)
- Exit-on-error releases allocator blocks before the exit hook (#30)
- `arg_str()` returns the chosen word of an `ARG_ENUM` (#30)

[Unreleased]: https://github.com/seesee010/Better-arg/compare/fc9d86a...HEAD
