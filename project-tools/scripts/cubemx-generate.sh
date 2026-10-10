#!/bin/sh

set -eu

: "${CUBEMX_EXECUTABLE:?CUBEMX_EXECUTABLE is required}"
: "${CUBEMX_IOC:?CUBEMX_IOC is required}"
: "${CUBEMX_OUTPUT_DIR:?CUBEMX_OUTPUT_DIR is required}"

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)

case "$CUBEMX_IOC" in
    /*) ioc_path=$CUBEMX_IOC ;;
    *)  ioc_path=$repo_root/$CUBEMX_IOC ;;
esac

case "$CUBEMX_OUTPUT_DIR" in
    /*) output_dir=$CUBEMX_OUTPUT_DIR ;;
    *)  output_dir=$repo_root/$CUBEMX_OUTPUT_DIR ;;
esac

if [ ! -f "$ioc_path" ]; then
    echo "error: CubeMX IOC file not found: $ioc_path" >&2
    exit 2
fi

mkdir -p "$output_dir"

# STM32CubeMX 6.x uses the directory containing the loaded IOC as the project
# root for this multi-context project.  Load a temporary copy from the build
# tree so generated FSBL/Appli/Drivers files stay out of the source tree.
generated_ioc=$output_dir/$(basename "$ioc_path")
if [ "$ioc_path" != "$generated_ioc" ]; then
    cp "$ioc_path" "$generated_ioc"
    ioc_path=$generated_ioc
fi

script_file=$(mktemp "${TMPDIR:-/tmp}/uai-cubemx.XXXXXX")
trap 'rm -f "$script_file"' EXIT HUP INT TERM

cat >"$script_file" <<EOF
config load "$ioc_path"
project generate "$output_dir"
exit
EOF

"$CUBEMX_EXECUTABLE" -q "$script_file"
