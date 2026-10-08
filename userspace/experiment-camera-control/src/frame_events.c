#include "stm32n6xx_hal.h"
void experiment_frame_wake(void);
void experiment_original_frame(DCMIPP_HandleTypeDef *, uint32_t);
void experiment_original_vsync(DCMIPP_HandleTypeDef *, uint32_t);

void HAL_DCMIPP_PIPE_FrameEventCallback(DCMIPP_HandleTypeDef *handle, uint32_t pipe)
{
    experiment_original_frame(handle, pipe);
    experiment_frame_wake();
}

void HAL_DCMIPP_PIPE_VsyncEventCallback(DCMIPP_HandleTypeDef *handle, uint32_t pipe)
{
    experiment_original_vsync(handle, pipe);
    experiment_frame_wake();
}