# sample-ai model generation

`generate_model.sh` generates the Neural-ART files used by `sample-ai` for
the three supported model families:

| name | official application | source model | xSPI2 model address | xSPI2 command blob address |
| --- | --- | --- | --- | --- |
| `person` | [STM32N6-GettingStarted-ObjectDetection](https://github.com/STMicroelectronics/STM32N6-GettingStarted-ObjectDetection) | `st_yolo_x_nano_480_1.0_0.25_3_st_int8.tflite` | `0x70380000` | `0x70500000` |
| `segmentation` | [STM32N6-GettingStarted-SemanticSegmentation](https://github.com/STMicroelectronics/STM32N6-GettingStarted-SemanticSegmentation) | `deeplab_v3_mobilenetv2_05_16_320_fft_qdq_int8.onnx` | `0x70600000` | `0x70560000` |
| `face` | [STM32N6-GettingStarted-FaceDetection](https://github.com/STMicroelectronics/STM32N6-GettingStarted-FaceDetection) | `blazeface_front_128_quant_pc_ff_od_wider_face.tflite` | `0x70800000` | `0x70580000` |

The official repositories are complete STM32N6 applications, not a runtime
library that needs to be linked into this project. The generator uses their
model files and the local `STEdgeAI` installation. Keeping the official
application repositories as submodules is optional; passing a model path is
enough and avoids importing their duplicate application, BSP, and middleware
trees into `sample-ai`.

Example:

```sh
sh userspace/sample-ai/models/generate_model.sh person \
  /path/to/st_yolo_x_nano_480_1.0_0.25_3_st_int8.tflite

sh userspace/sample-ai/models/generate_model.sh segmentation \
  /path/to/deeplab_v3_mobilenetv2_05_16_320_fft_qdq_int8.onnx

sh userspace/sample-ai/models/generate_model.sh face \
  /path/to/blazeface_front_128_quant_pc_ff_od_wider_face.tflite
```

If the model is placed under `models/source/<name>/` with the filename shown
above, the second argument can be omitted. The source model files are ignored
by git because they are large and may have separate model licenses.

The generator uses `--no-inputs-allocation` and `--no-outputs-allocation` so
the application can provide the Pipe2 input buffer. Use the same STEdgeAI
and `ll_aton` runtime generation version as the checked-in generated files;
otherwise the generated C files can fail the Neural-ART version check.

Generation can be tuned without editing the script:

```sh
AI_MODEL_OPTIMIZATION=time \
AI_MODEL_INPUT_DATA_TYPE=uint8 \
AI_MODEL_OUTPUT_DATA_TYPE=int8 \
sh userspace/sample-ai/models/generate_model.sh person /path/to/model.tflite
```

The corresponding variables are `AI_MODEL_OPTIMIZATION`,
`AI_MODEL_INPUT_DATA_TYPE`, `AI_MODEL_OUTPUT_DATA_TYPE`,
`AI_MODEL_INPUTS_CH_POSITION`, `AI_MODEL_OUTPUTS_CH_POSITION`,
`AI_MODEL_C_API`, and `AI_MODEL_NETWORK_ADDRESS`. The latter overrides the
model-specific xSPI2 address when using a different Flash layout. The default C API is `st-ai`; it is required for the
experimental runtime activation/state allocation options.

`model1` and `model2` are legacy directories and are no longer accepted by
the generator.

Each model's `network_blobs.hex` must be programmed at its corresponding
command blob address above. sample-ai always links all three models; the linker
emits three sections and three separate command-blob images:

| model | section | image | address |
| --- | --- | --- | ---: |
| person | `.network_blobs_person` | `network_blobs_person.hex` | `0x70500000` |
| segmentation | `.network_blobs_segmentation` | `network_blobs_segmentation.hex` | `0x70560000` |
| face | `.network_blobs_face` | `network_blobs_face.hex` | `0x70580000` |

At startup sample-ai initializes all three generated network contexts. This
copies each EC command blob into its runtime buffer once; switching models then
reuses the resident buffers and shared activation RAM.
