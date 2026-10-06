# experiment-camera-lcd-touch driver layout

experiment-camera-lcd-touch owns its camera, display, touch, overlay, frame-buffer, HAL time, and
interrupt code under `userspace/experiment-camera-lcd-touch/src/driver/`. No driver implementation
is added under `kernel/driver`; the implementation is self-contained within this experiment.

The application links the official STM32CubeN6 BSP, IMX335 and GT911 components, ISP middleware,
and HAL sources directly from `STM32CUBE_N6_DIR`. Its linker script reserves separate areas for the
application image and RAM-run stack, two composed display frames, and the camera capture frame.
