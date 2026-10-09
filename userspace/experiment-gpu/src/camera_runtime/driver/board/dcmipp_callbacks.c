#include "isp_api.h"
#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_camera.h"

extern ISP_HandleTypeDef hcamera_isp;

volatile unsigned int camera_pipe2_pipe1_vsync_count;
volatile unsigned int camera_pipe2_pipe2_frame_count;

/* The BSP callback treats every pipe as the main pipe.  Use the ISP template
 * convention instead: Pipe1 is the main output and Pipe2 is ancillary. */
void HAL_DCMIPP_PIPE_VsyncEventCallback(
    DCMIPP_HandleTypeDef *hdcmipp,
    uint32_t pipe
)
{
    UNUSED(hdcmipp);
    if (pipe == DCMIPP_PIPE1) {
        ++camera_pipe2_pipe1_vsync_count;
        ISP_IncMainFrameId(&hcamera_isp);
        ISP_GatherStatistics(&hcamera_isp);
        ISP_OutputMeta(&hcamera_isp);
    }
    BSP_CAMERA_VsyncEventCallback(0);
}

void HAL_DCMIPP_PIPE_FrameEventCallback(
    DCMIPP_HandleTypeDef *hdcmipp,
    uint32_t pipe
)
{
    UNUSED(hdcmipp);
    if (pipe == DCMIPP_PIPE2) {
        ++camera_pipe2_pipe2_frame_count;
        ISP_IncAncillaryFrameId(&hcamera_isp);
    }
    BSP_CAMERA_FrameEventCallback(0);
}
