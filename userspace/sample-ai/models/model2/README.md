# model2

MobileNet v1 0.25, 96x96, quantized TFLite model. It exercises the same
Neural-ART path with a smaller model and is useful when model1 is too large for
an early smoke test.

Generate it with the STM32N6570-DK memory profile:

```sh
sh userspace/sample-ai/models/generate_model.sh model2 \
  /path/to/mobilenet_v1_0.25_96_tfs_int8.tflite
```
