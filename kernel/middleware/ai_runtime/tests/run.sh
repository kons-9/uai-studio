#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
output=$(mktemp "${TMPDIR:-/tmp}/uai-ai-runtime-test.XXXXXX")
trap 'rm -f "$output"' EXIT
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/kernel" \
    "$root/kernel/middleware/ai_runtime/pipeline.cpp" \
    "$root/kernel/middleware/ai_runtime/tests/ai_runtime_test.cpp" \
    -o "$output"
"$output"
