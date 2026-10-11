#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# check-style.sh - mechanical checks from docs/coding-style.rst
#
# Fails on trailing whitespace and on indentation with spaces, and
# reports lines longer than 80 columns (tabs count as 8).
set -eu

cd "$(dirname "$0")/.."
files=$(git ls-files '*.c' '*.h')
[ -n "$files" ] || exit 0
status=0

# shellcheck disable=SC2086
if grep -nE '[[:space:]]+$' $files; then
	echo "error: trailing whitespace (see above)" >&2
	status=1
fi

# Leading spaces are allowed only for " * " block comment lines.
# shellcheck disable=SC2086
if grep -nE '^ +[^ *]' $files; then
	echo "error: indentation must use tabs (see above)" >&2
	status=1
fi

# shellcheck disable=SC2086
long=$(awk '{ line = $0; gsub(/\t/, "        ", line);
	      if (length(line) > 80) print FILENAME ":" FNR ": " length(line) " columns" }' $files)
if [ -n "$long" ]; then
	echo "$long" | while IFS= read -r l; do
		echo "::warning::line longer than 80 columns: $l"
	done
fi

exit $status
