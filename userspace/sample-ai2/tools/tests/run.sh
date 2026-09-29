#!/bin/sh
set -eu

app_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
python3 -m unittest discover -s "$app_dir/tools/tests" -p 'test_*.py'

output=$(mktemp "${TMPDIR:-/tmp}/sample-ai2-middleware.XXXXXX")
trap 'rm -f "$output"' EXIT HUP INT TERM
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$app_dir/src" -I"$app_dir/src/middleware" \
    "$app_dir/tools/tests/middleware_smoke.cpp" \
    "$app_dir/src/middleware/image_resizer/image_resizer.cpp" \
    "$app_dir/src/driver/camera_driver/dcmipp_resize.cpp" \
    -o "$output"
"$output"
