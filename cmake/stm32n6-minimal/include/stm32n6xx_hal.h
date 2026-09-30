/*
 * Minimal STM32N6 HAL compatibility header for the µT-Kernel hello-world
 * target.
 *
 * BSP2 includes the family HAL header unconditionally from its generic device
 * header.  experiment-hello-world does not enable any HAL-backed I2C/ADC driver, so no HAL
 * declarations are needed to compile this target.  Pass
 * -DSTM32CUBE_N6_DIR=/path/to/STM32CubeN6 when those drivers or Cube HAL
 * initialization are introduced.
 */
#ifndef UAI_STUDIO_MINIMAL_STM32N6XX_HAL_H
#define UAI_STUDIO_MINIMAL_STM32N6XX_HAL_H

#endif
