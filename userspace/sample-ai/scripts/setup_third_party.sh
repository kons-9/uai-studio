#!/bin/sh
set -eu

# This script intentionally does not copy the STEdgeAI runtime.  That runtime
# is distributed by ST and must remain version-matched with the generated
# network C files.  It only resolves and validates the two dependency roots
# used by the sample, then prints the exact CMake arguments.

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
sample_ai_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
project_dir=$(CDPATH= cd -- "$sample_ai_dir/../.." && pwd)

runtime_dir=${1:-${STEDGEAI_LIB_DIR:-}}
if [ -z "$runtime_dir" ]; then
    for candidate in \
        /opt/ST/STEdgeAI/4.0/Middlewares/ST/AI \
        /opt/ST/STEdgeAI/*/Middlewares/ST/AI
    do
        if [ -f "$candidate/Npu/ll_aton/ll_aton_version.h" ]; then
            runtime_dir=$candidate
            break
        fi
    done
fi

if [ -z "$runtime_dir" ] || [ ! -f "$runtime_dir/Npu/ll_aton/ll_aton_version.h" ]; then
    echo "STEdgeAI runtime was not found." >&2
    echo "Install STEdgeAI and set STEDGEAI_LIB_DIR to its Middlewares/ST/AI directory." >&2
    exit 1
fi

for required_path in \
    "$runtime_dir/Inc" \
    "$runtime_dir/Npu/ll_aton" \
    "$runtime_dir/Npu/Devices/STM32N6xx"
do
    if [ ! -d "$required_path" ]; then
        echo "Missing STEdgeAI runtime directory: $required_path" >&2
        exit 1
    fi
done

runtime_archive=
for archive_name in NetworkRuntime1201_CM55_GCC.a NetworkRuntime1200_CM55_GCC.a; do
    if [ -f "$runtime_dir/Lib/GCC/ARMCortexM55/$archive_name" ]; then
        runtime_archive=$runtime_dir/Lib/GCC/ARMCortexM55/$archive_name
        break
    fi
done
if [ -z "$runtime_archive" ]; then
    echo "No CM55 GCC NetworkRuntime archive was found under $runtime_dir/Lib/GCC/ARMCortexM55." >&2
    exit 1
fi

postprocess_dir=${AI_VISION_MODELS_PP_DIR:-$sample_ai_dir/third_party/vision_models_pp}
for required_path in \
    "$postprocess_dir/Inc/od_st_yolox_pp_if.h" \
    "$postprocess_dir/Inc/fd_blazeface_pp_if.h" \
    "$postprocess_dir/Src/od_pp_st_yolox.c" \
    "$postprocess_dir/Src/fd_pp_blazeface.c" \
    "$postprocess_dir/Src/vision_models_pp.c" \
    "$postprocess_dir/Src/vision_models_pp_maxi_is8.c"
do
    if [ ! -f "$required_path" ]; then
        echo "Missing sample-ai post-processing file: $required_path" >&2
        exit 1
    fi
done

model_dev=$(sed -n 's/.*LL_ATON_VERSION_DEV != (\([0-9][0-9]*\)).*/\1/p' \
    "$sample_ai_dir/models/person/network.c" | head -n 1)
runtime_dev=$(sed -n 's/.*LL_ATON_VERSION_DEV[[:space:]]*(\([0-9][0-9]*\)).*/\1/p' \
    "$runtime_dir/Npu/ll_aton/ll_aton_version.h" | head -n 1)
if [ -n "$model_dev" ] && [ -n "$runtime_dev" ] && [ "$model_dev" != "$runtime_dev" ]; then
    echo "ll_aton version mismatch: model=$model_dev runtime=$runtime_dev" >&2
    exit 1
fi

echo "sample-ai dependencies are ready."
echo "  STEdgeAI:       $runtime_dir"
echo "  runtime archive: $runtime_archive"
echo "  post-processing: $postprocess_dir"
echo
echo "Configure from the repository root with:"
echo "  cmake -S \"$project_dir\" -B build-sample-ai \\"
echo "    -DAPP_TARGET=sample-ai \\"
echo "    -DSTEDGEAI_LIB_DIR=\"$runtime_dir\" \\"
echo "    -DAI_VISION_MODELS_PP_DIR=\"$postprocess_dir\""
