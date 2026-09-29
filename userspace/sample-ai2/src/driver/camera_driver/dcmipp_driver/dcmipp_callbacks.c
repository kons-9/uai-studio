#include "isp_api.h"
#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_camera.h"

extern ISP_HandleTypeDef hcamera_isp;

extern void AiCameraPipe2FrameEventCallback(void);

/* Camera driver callbacks preserve the pipe identity; the stock BSP forwards
 * callbacks from every pipe to the Pipe1 callback.
 * sample-ai uses Pipe1 for display and Pipe2 for the RGB888 NN input, so the
 * callbacks must retain the pipe identity. */
void HAL_DCMIPP_PIPE_VsyncEventCallback(DCMIPP_HandleTypeDef *hdcmipp,
                                        uint32_t pipe)
{
    UNUSED(hdcmipp);
    if (pipe == DCMIPP_PIPE1) {
        ISP_IncMainFrameId(&hcamera_isp);
        ISP_GatherStatistics(&hcamera_isp);
        ISP_OutputMeta(&hcamera_isp);
        BSP_CAMERA_VsyncEventCallback(0U);
    }
}

void HAL_DCMIPP_PIPE_FrameEventCallback(DCMIPP_HandleTypeDef *hdcmipp,
                                        uint32_t pipe)
{
    UNUSED(hdcmipp);
    if (pipe == DCMIPP_PIPE1) {
        BSP_CAMERA_FrameEventCallback(0U);
    } else if (pipe == DCMIPP_PIPE2) {
        ISP_IncAncillaryFrameId(&hcamera_isp);
        AiCameraPipe2FrameEventCallback();
    }
}
