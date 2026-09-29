#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
output=$(mktemp "${TMPDIR:-/tmp}/uai-ai-runtime-test.XXXXXX")
trap 'rm -f "$output"' EXIT
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" \
    "$root/src/middleware/ai_runtime/pipeline.cpp" \
    "$root/src/middleware/ai_runtime/tests/ai_runtime_test.cpp" \
    -o "$output"
"$output"
