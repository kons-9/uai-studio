# experiment-camera-lcd driver layout

experiment-camera-lcd owns its camera, display, frame-buffer, HAL time, and interrupt code
under `userspace/experiment-camera-lcd/src/driver/`. No driver implementation is added under
`kernel/driver`; the implementation is self-contained within experiment-camera-lcd.

The application links the official STM32CubeN6 BSP, IMX335 component, ISP
middleware, and HAL sources directly from `STM32CUBE_N6_DIR`. Its linker script
is also kept in this directory because the camera/ISP image is executed from
the contiguous STM32N657 SRAM.
