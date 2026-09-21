# Changes outside `sample1/`

`sample1` keeps the original LCD/camera initialization sequence, but the
implementation is now linked from `kernel/driver` through `DisplayDriver` and
`CameraDriver`. The application only sequences the two classes and runs the
camera background process.

The required changes outside this directory are:

- `CMakeLists.txt`: add `sample1` to the application choices and add the
  `kernel/driver` subdirectory when `APP_TARGET` is `sample1`.
- `kernel/driver/`: contains the `CameraDriver`/`DisplayDriver` classes and the
  STM32N6570-DK implementation, including the LCD/camera BSP, DCMIPP/LTDC/
  DMA2D/RIF HAL sources, media SRAM setup, HAL time functions, and camera IRQs.
- `userspace/sample1/CMakeLists.txt` and `userspace/sample1/src/`: now contain
  only the application sequencing code; the working BSP call order remains in
  the driver architecture layer.
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
