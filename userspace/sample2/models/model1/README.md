# model1

EfficientNet v2 B1, 240x240, quantized ONNX model. This is the model used by
ST's STM32N6570-DK image-classification example.

The generated files are intentionally not committed here. Generate them with:

```sh
sh userspace/sample2/models/generate_model.sh model1 \
  /path/to/efficientnet_v2B1_240_fft_qdq_int8.onnx
```
