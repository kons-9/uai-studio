#!/usr/bin/env bash

set -euo pipefail

case "${1:---check}" in
    --check) format_args=(--dry-run --Werror) ;;
    --fix) format_args=(-i) ;;
    *) printf 'Usage: bash build-system/scripts/format.sh [--check|--fix]\n' >&2; exit 2 ;;
esac
if (( $# > 1 )); then
    printf 'Usage: bash build-system/scripts/format.sh [--check|--fix]\n' >&2
    exit 2
fi

repo_root=$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)
cd "$repo_root"

formatter=${CLANG_FORMAT:-clang-format-21}
version=$("$formatter" --version)
if [[ ! "$version" =~ version[[:space:]]21\. ]]; then
    printf 'clang-format 21 is required; found: %s\n' "$version" >&2
    exit 2
fi

git ls-files -z -- '*.c' '*.h' '*.cc' '*.hh' '*.cpp' '*.hpp' '*.cxx' '*.hxx' '*.inc' '*.ipp' |
    xargs -0 -r "$formatter" --style=file "${format_args[@]}"