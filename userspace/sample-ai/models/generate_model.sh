#!/usr/bin/env sh

set -eu

models_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
model_name=${1:-}

if [ -z "$model_name" ]; then
    echo "usage: $0 person|segmentation|face [model-file]" >&2
    exit 2
fi

case "$model_name" in
    person)
        model_dir="$models_dir/person"
        default_source="$models_dir/source/person/st_yolo_x_nano_480_1.0_0.25_3_st_int8.tflite"
        network_address=0x70380000
        default_outputs_ch_position=chlast
        official_source="STMicroelectronics/STM32N6-GettingStarted-ObjectDetection/Model"
        ;;
    segmentation)
        model_dir="$models_dir/segmentation"
        default_source="$models_dir/source/segmentation/deeplab_v3_mobilenetv2_05_16_320_fft_qdq_int8.onnx"
        network_address=0x70600000
        default_outputs_ch_position=chlast
        default_cut_output_tensors='model/conv2d_6/BiasAdd:0_QuantizeLinear_Output'
        official_source="STMicroelectronics/STM32N6-GettingStarted-SemanticSegmentation/Model/STM32N6570-DK"
        ;;
    face)
        model_dir="$models_dir/face"
        default_source="$models_dir/source/face/blazeface_front_128_quant_pc_ff_od_wider_face.tflite"
        network_address=0x70800000
        default_outputs_ch_position=chfirst
        official_source="STMicroelectronics/STM32N6-GettingStarted-FaceDetection/Model"
        ;;
    *)
        echo "usage: $0 person|segmentation|face [model-file]" >&2
        exit 2
        ;;
esac

default_cut_output_tensors=${default_cut_output_tensors:-}

# Keep the generated model data at a model-specific address.  This can be
# overridden for a board-specific Flash layout, but must be used consistently
# when generating the network and when programming network_data.xSPI2.bin.
network_address=${AI_MODEL_NETWORK_ADDRESS:-$network_address}
memory_pool="$models_dir/my_mpools/stm32n6-app2_STM32N6570-DK.mpool"

input_data_type=${AI_MODEL_INPUT_DATA_TYPE:-uint8}
output_data_type=${AI_MODEL_OUTPUT_DATA_TYPE:-int8}
inputs_ch_position=${AI_MODEL_INPUTS_CH_POSITION:-chlast}
outputs_ch_position=${AI_MODEL_OUTPUTS_CH_POSITION:-$default_outputs_ch_position}
optimization=${AI_MODEL_OPTIMIZATION:-balanced}
c_api=${AI_MODEL_C_API:-st-ai}
cut_output_tensors=${AI_MODEL_CUT_OUTPUT_TENSORS:-$default_cut_output_tensors}

model_source=${2:-${AI_MODEL_SOURCE:-$default_source}}
case "$model_source" in
    /*) ;;
    *) model_source="$PWD/$model_source" ;;
esac

if [ ! -f "$model_source" ]; then
    echo "model source does not exist: $model_source" >&2
    echo "Obtain the official model from $official_source" >&2
    echo "or pass its path as the second argument / set AI_MODEL_SOURCE." >&2
    exit 1
fi

if ! command -v stedgeai >/dev/null 2>&1; then
    echo "stedgeai was not found in PATH." >&2
    exit 1
fi
if ! command -v arm-none-eabi-objcopy >/dev/null 2>&1; then
    echo "arm-none-eabi-objcopy was not found in PATH." >&2
    exit 1
fi

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/sample-ai-${model_name}.XXXXXX")
cleanup() {
    rm -rf "$work_dir"
}
trap cleanup EXIT HUP INT TERM

# Neural-ART does not accept --address directly. The xSPI2 pool origin is the
# model data address, so create a per-run pool descriptor with the selected
# origin. This keeps the checked-in common pool reusable for all three models.
model_memory_pool="$work_dir/model.mpool"
sed "s/0x70380000/$network_address/g" "$memory_pool" > "$model_memory_pool"
neural_art_config="$work_dir/user_neuralart.json"
sed "s|\"memory_pool\": \"[^\"]*\"|\"memory_pool\": \"$model_memory_pool\"|" \
    "$models_dir/user_neuralart_STM32N6570-DK.json" > "$neural_art_config"

# These are the options used by the STM32N6 application examples. Input and
# output buffers are supplied by the application, which is required for the
# Pipe2-to-NPU path used by sample-ai.
cd "$models_dir"
set -- \
    --no-inputs-allocation \
    --no-outputs-allocation \
    --optimization "$optimization" \
    --c-api "$c_api" \
    --model "$model_source" \
    --target stm32n6 \
    --st-neural-art "default@$neural_art_config" \
    --memory-pool "$model_memory_pool" \
    --input-data-type "$input_data_type" \
    --output-data-type "$output_data_type" \
    --inputs-ch-position "$inputs_ch_position" \
    --outputs-ch-position "$outputs_ch_position" \
    --workspace "$work_dir/st_ai_ws" \
    --output "$work_dir/st_ai_output"
if [ -n "$cut_output_tensors" ]; then
    set -- "$@" --cut-output-tensors "$cut_output_tensors"
fi
stedgeai generate "$@"

for generated_file in network.c network_ecblobs.h stai_network.c stai_network.h network_atonbuf.xSPI2.raw; do
    if [ ! -f "$work_dir/st_ai_output/$generated_file" ]; then
        echo "STEdgeAI did not generate $generated_file" >&2
        exit 1
    fi
done

cp "$work_dir/st_ai_output/network.c" "$model_dir/"
cp "$work_dir/st_ai_output/network_ecblobs.h" "$model_dir/"
cp "$work_dir/st_ai_output/stai_network.c" "$model_dir/"
cp "$work_dir/st_ai_output/stai_network.h" "$model_dir/"
cp "$work_dir/st_ai_output/network_atonbuf.xSPI2.raw" "$model_dir/network_data.xSPI2.bin"

# The address is model-specific. Keep the address in the generated network,
# the binary/Intel HEX artifact, and the Flash programming command aligned.
arm-none-eabi-objcopy -I binary \
    "$model_dir/network_data.xSPI2.bin" \
    --change-addresses "$network_address" \
    -O ihex "$model_dir/network_data.hex"

# Keep the legacy/manual programming filename synchronized with the generated
# artifact.  The Intel HEX records contain absolute memory addresses; creating
# a second shifted copy makes the generated network.c and Flash image disagree.
cp "$model_dir/network_data.hex" "$model_dir/network_data_flash.hex"

echo "Generated $model_name in $model_dir"
echo "Model data address: $network_address"
echo "Program $model_dir/network_data.hex to the DK XSPI2 model area before running sample-ai."
