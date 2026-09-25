#include <tk/tkernel.h>

#include "driver/camera_driver.hpp"
#include "driver/display_driver.hpp"
#include "driver/frame_buffer.hpp"

/* T-MonitorヘッダーはC APIなので、C++でもC ABIとして宣言する。 */
extern "C" {
#include <tm/tmonitor.h>
#include "stm32n6xx_hal.h"
#include "imx335_reg.h"
#include "stm32n6570_discovery_bus.h"
#include "stm32n6570_discovery_camera.h"
#include "stm32n6570_discovery_lcd.h"
}

extern DCMIPP_HandleTypeDef hcamera_dcmipp;
extern LTDC_HandleTypeDef hlcd_ltdc;

namespace {

void halt_with_message(const char *message)
{
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(message)));
    for (;;) {
        tk_dly_tsk(1000);
    }
}

} // namespace

/* µT-Kernelから呼び出されるユーザータスクのエントリーポイント。 */
extern "C" INT usermain(void)
{
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "sample1: initializing LCD and IMX335 camera\n")));

    uai::sample1::driver::DisplayDriver display;
    uai::sample1::driver::CameraDriver camera;

    if (!uai::sample1::driver::IsOk(display.Initialize())) {
        halt_with_message("sample1: display initialization failed\n");
    }

    /* Stage 1 diagnostic: show a solid RGB565 red frame before camera start. */
    for (std::size_t offset = 0U;
         offset < uai::sample1::driver::kFrameBytes;
         offset += 2U) {
        uai::sample1::driver::FrameBuffer()[offset] = 0x00U;
        uai::sample1::driver::FrameBuffer()[offset + 1U] = 0xf8U;
    }

    if (!uai::sample1::driver::IsOk(camera.Initialize())) {
        halt_with_message("sample1: camera initialization failed\n");
    }

    if (!uai::sample1::driver::IsOk(camera.Start())) {
        halt_with_message("sample1: camera start failed\n");
    }

    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "sample1: camera preview started\n")));

    uint8_t sensor_mode = 0xffU;
    const int32_t sensor_read_status = BSP_I2C1_ReadReg16(
        CAMERA_IMX335_ADDRESS, IMX335_REG_MODE_SELECT, &sensor_mode, 1U);

    tm_printf(reinterpret_cast<const UB *>(
        "sample1: regs dcmipp=%08x/%08x p1=%08x/%08x addr=%08x csi=%08x/%08x err=%08x/%08x ltdc=%08x bccr=%08x cdsr=%08x l1=%08x/%08x/%08x/%08x/%08x l2=%08x fb=%02x/%02x i2c=%ld mode=%02x\n"),
        DCMIPP->CMSR1,
        DCMIPP->CMSR2,
        DCMIPP->P1SR,
        DCMIPP->P1PPCR,
        DCMIPP->P1PPM0AR1,
        CSI->SR0,
        CSI->SR1,
        CSI->ERR1,
        CSI->ERR2,
        LTDC->GCR,
        LTDC->BCCR,
        LTDC->CDSR,
        LTDC_Layer1->CR,
        LTDC_Layer1->PFCR,
        LTDC_Layer1->CFBAR,
        LTDC_Layer1->CFBLR,
        LTDC_Layer1->CFBLNR,
        LTDC_Layer2->CFBAR,
        static_cast<UINT>(uai::sample1::driver::FrameBuffer()[0]),
        static_cast<UINT>(uai::sample1::driver::FrameBuffer()[1]),
        static_cast<long>(sensor_read_status),
        static_cast<UINT>(sensor_mode));

    for (;;) {
        if (!uai::sample1::driver::IsOk(camera.Process())) {
            halt_with_message("sample1: camera background process failed\n");
        }
        if (!uai::sample1::driver::IsOk(display.Process())) {
            halt_with_message("sample1: display process failed\n");
        }
        tk_dly_tsk(1);
    }
}
