# sample-ai third-party components

The directory is intentionally split by ownership:

- `vision_models_pp/` is the small, source-only ST vision-model post-processing
  component used by this application. It is kept in the repository so a clean
  checkout has the required C sources and headers.
- STEdgeAI's Neural-ART runtime (`Inc/`, `Npu/`, and `Lib/`) is not copied here.
  It is an external ST distribution and must be selected with
  `-DSTEDGEAI_LIB_DIR=/path/to/Middlewares/ST/AI`. CMake checks the generated
  model's `ll_aton` version and selects the matching CM55 GCC archive.

`vision_models_pp/` is sourced from STMicroelectronics' STM32N6 vision-model
post-processing component. `od_pp_st_yolox.c` contains the sample-ai safety
guard that prevents writes beyond `max_boxes_limit`; the remaining files are
used as provided for the supported person and face models.

For a local dependency check, run:

```sh
sh userspace/sample-ai/scripts/setup_third_party.sh
```
