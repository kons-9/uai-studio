# Changes outside `sample1/`

The current `sample1` is temporarily restored to the original monolithic LCD
and camera implementation because the separated display path produced a blank
panel on the target. The reusable `kernel/driver` implementation is retained
for the next A/B step, but is not linked by the current `sample1` executable.

The required changes outside this directory are:

- `CMakeLists.txt`: add `sample1` to the application choices and add the
  `kernel/driver` subdirectory when `APP_TARGET` is `sample1`.
- `kernel/driver/`: contains the common `CameraDriver`/`DisplayDriver` classes
  and the STM32N6570-DK implementation. These files are currently kept out of
  the `sample1` link while the original display path is being verified.
- `userspace/sample1/CMakeLists.txt` and `userspace/sample1/src/`: restored the
  original direct BSP/HAL/ISP source layout, including the LCD BSP, DCMIPP,
  LTDC, DMA2D, RIF, media SRAM setup, HAL time functions, and camera IRQs.
- `Makefile`: export `STM32CUBE_N6_DIR` and pass it from the
  command line/environment into CMake so `kernel/driver` can locate the
  official STM32CubeN6 BSP and ISP sources.
- `config/local.mk`: set the host-specific `STM32CUBE_N6_DIR` path. The
  repository's `config/local.mk.example` documents this setting.

`cmake/stm32_cli.cmake` already selected the IOC belonging to `APP_TARGET`, so
it did not need to be changed. This lets `APP_TARGET=sample1` use
`userspace/sample1/config/stm32n6570-dk-fullsecure.ioc`.

No STM32CubeN6 source file is copied into the repository or modified. The
sample1 CMake file consumes the BSP and ISP sources from `STM32CUBE_N6_DIR`.
