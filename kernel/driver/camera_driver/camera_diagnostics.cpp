#include "driver/camera_driver/camera_diagnostics.hpp"

#include "middleware/foundation/log.hpp"

extern "C" {
#include "stm32n6xx_hal.h"

extern DCMIPP_HandleTypeDef hcamera_dcmipp;
extern volatile unsigned int g_ai_last_exposure_request_us;
extern volatile unsigned int g_ai_last_exposure_lines;
int32_t AiGetSensorGainMdB(void);
int32_t AiReadSensorRegisters(
    std::uint32_t *vmax,
    std::uint32_t *shutter,
    std::uint32_t *gain
);
}

namespace uai::ai::camera {

SensorDiagnostics ReadSensorDiagnostics()
{
    SensorDiagnostics result{};
    result.register_status = AiReadSensorRegisters(&result.vmax, &result.shutter, &result.gain);
    result.exposure_us = g_ai_last_exposure_request_us;
    result.exposure_lines = g_ai_last_exposure_lines;
    result.gain_mdB = AiGetSensorGainMdB();
    return result;
}

void DumpCaptureRegisters(
    const pipeline::CaptureFrame &frame,
    const Diagnostics &diagnostics,
    std::uint32_t crc
)
{
    if (!common::IsLogEnabled(common::LogLevel::kDebug))
        return;
    UAI_LOG_DEBUG(
        "camera: frozen raw sequence=%u address=%x bytes=%u crc=%x frames=%u cptact=%x m0ar=%x\n",
        static_cast<unsigned int>(frame.sequence),
        static_cast<unsigned int>(frame.buffer.address),
        static_cast<unsigned int>(frame.buffer.size),
        crc,
        diagnostics.frame_event_count,
        static_cast<unsigned int>(DCMIPP->CMSR1 & DCMIPP_CMSR1_P1CPTACT),
        static_cast<unsigned int>(
            HAL_DCMIPP_PIPE_GetMemoryAddress(&hcamera_dcmipp, DCMIPP_PIPE1, DCMIPP_MEMORY_ADDRESS_0)
        )
    );
    UAI_LOG_DEBUG(
        "camera: P1 regs fscr=%x fctcr=%x sr=%x crop=%x/%x down=%x/%x pack=%x pitch=%x m0=%x/%x csi=%x/%x counts=%u "
        "sot_sync=%u/%u sot=%u/%u pend=%x/%x\n",
        static_cast<unsigned int>(DCMIPP->P1FSCR),
        static_cast<unsigned int>(DCMIPP->P1FCTCR),
        static_cast<unsigned int>(DCMIPP->P1SR),
        static_cast<unsigned int>(DCMIPP->P1CRSTR),
        static_cast<unsigned int>(DCMIPP->P1CRSZR),
        static_cast<unsigned int>(DCMIPP->P1DSRTIOR),
        static_cast<unsigned int>(DCMIPP->P1DSSZR),
        static_cast<unsigned int>(DCMIPP->P1PPCR),
        static_cast<unsigned int>(DCMIPP->P1PPM0PR),
        static_cast<unsigned int>(DCMIPP->P1PPM0AR1),
        static_cast<unsigned int>(DCMIPP->P1PPM0AR2),
        static_cast<unsigned int>(CSI->SR0),
        static_cast<unsigned int>(CSI->SR1),
        diagnostics.csi_error_count,
        diagnostics.csi_sot_sync_dl0_count,
        diagnostics.csi_sot_sync_dl1_count,
        diagnostics.csi_sot_dl0_count,
        diagnostics.csi_sot_dl1_count,
        diagnostics.csi_last_pending_status,
        diagnostics.csi_last_pending_status1
    );
    UAI_LOG_DEBUG(
        "camera: P1 detail cfscr=%x cfctcr=%x decr=%x dmcr=%x "
        "cdccr=%x cdscr=%x cdsrtior=%x cdsszr=%x "
        "cppcr=%x cppitch=%x cpm0=%x/%x\n",
        static_cast<unsigned int>(DCMIPP->P1CFSCR),
        static_cast<unsigned int>(DCMIPP->P1CFCTCR),
        static_cast<unsigned int>(DCMIPP->P1DECR),
        static_cast<unsigned int>(DCMIPP->P1DMCR),
        static_cast<unsigned int>(DCMIPP->P1CDCCR),
        static_cast<unsigned int>(DCMIPP->P1CDSCR),
        static_cast<unsigned int>(DCMIPP->P1CDSRTIOR),
        static_cast<unsigned int>(DCMIPP->P1DSSZR),
        static_cast<unsigned int>(DCMIPP->P1CPPCR),
        static_cast<unsigned int>(DCMIPP->P1CPPM0PR),
        static_cast<unsigned int>(DCMIPP->P1CPPM0AR1),
        static_cast<unsigned int>(DCMIPP->P1CPPM0AR2)
    );
    UAI_LOG_DEBUG(
        "camera: CSI cfg pfcr=%x pcr=%x vc0=%x/%x/%x/%x\n",
        static_cast<unsigned int>(CSI->PFCR),
        static_cast<unsigned int>(CSI->PCR),
        static_cast<unsigned int>(CSI->VC0CFGR1),
        static_cast<unsigned int>(CSI->VC0CFGR2),
        static_cast<unsigned int>(CSI->VC0CFGR3),
        static_cast<unsigned int>(CSI->VC0CFGR4)
    );
}

} // namespace uai::ai::camera