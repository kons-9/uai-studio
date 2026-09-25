#!/usr/bin/env sh

set -eu

models_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
model_name=${1:-model1}
model_dir="$models_dir/$model_name"

case "$model_name" in
    model1)
        default_source="$models_dir/source/efficientnet_v2B1_240_fft_qdq_int8.onnx"
        model_options="--input-data-type uint8 --output-data-type float32 --inputs-ch-position chlast"
        ;;
    model2)
        default_source="$models_dir/source/mobilenet_v1_0.25_96_tfs_int8.tflite"
        model_options="--input-data-type uint8"
        ;;
    *)
        echo "usage: $0 model1|model2 [model-file]" >&2
        exit 2
        ;;
esac

model_source=${2:-${AI_MODEL_SOURCE:-$default_source}}
case "$model_source" in
    /*) ;;
    *) model_source="$PWD/$model_source" ;;
esac

if [ ! -f "$model_source" ]; then
    echo "model source does not exist: $model_source" >&2
    echo "Pass the source path as the second argument or set AI_MODEL_SOURCE." >&2
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

cd "$models_dir"
stedgeai generate \
    --model "$model_source" \
    --target stm32n6 \
    --st-neural-art default@user_neuralart_STM32N6570-DK.json \
    $model_options

for generated_file in network.c network_ecblobs.h stai_network.c stai_network.h network_atonbuf.xSPI2.raw; do
    if [ ! -f "st_ai_output/$generated_file" ]; then
        echo "STEdgeAI did not generate st_ai_output/$generated_file" >&2
        exit 1
    fi
done

cp st_ai_output/network.c "$model_dir/"
cp st_ai_output/network_ecblobs.h "$model_dir/"
cp st_ai_output/stai_network.c "$model_dir/"
cp st_ai_output/stai_network.h "$model_dir/"
cp st_ai_output/network_atonbuf.xSPI2.raw "$model_dir/network_data.xSPI2.bin"

arm-none-eabi-objcopy -I binary \
    "$model_dir/network_data.xSPI2.bin" \
    --change-addresses 0x70380000 \
    -O ihex "$model_dir/network_data.hex"

echo "Generated $model_name in $model_dir"
echo "Program $model_dir/network_data.hex to the DK XSPI2 model area before running sample-ai."
