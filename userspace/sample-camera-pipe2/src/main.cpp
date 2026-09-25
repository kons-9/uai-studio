#include <cstddef>
#include <cstdint>

#include <tk/tkernel.h>

#include "driver/camera_driver.hpp"
#include "driver/display_driver.hpp"
#include "driver/frame_buffer.hpp"

extern "C" {
#include <tm/tmonitor.h>
#include "stm32n6xx_hal.h"

extern volatile unsigned int camera_pipe2_pipe1_vsync_count;
extern volatile unsigned int camera_pipe2_pipe2_frame_count;
}

namespace {

void FillRgb565(std::uint8_t *buffer, std::uint16_t color)
{
    for (std::size_t offset = 0U;
         offset < uai::camera_pipe2::driver::kFrameBytes; offset += 2U) {
        buffer[offset] = static_cast<std::uint8_t>(color & 0xffU);
        buffer[offset + 1U] = static_cast<std::uint8_t>(color >> 8U);
    }
}

void halt_with_message(const char *message)
{
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(message)));
    for (;;) {
        tk_dly_tsk(1000);
    }
}

} // namespace

extern "C" INT usermain(void)
{
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "camera_pipe2: initializing LCD and two camera pipes\n")));

    uai::camera_pipe2::driver::DisplayDriver display;
    uai::camera_pipe2::driver::CameraDriver camera;

    if (!uai::camera_pipe2::driver::IsOk(display.Initialize())) {
        halt_with_message("camera_pipe2: display initialization failed\n");
    }

    /* Make both LTDC layers visible while the camera is being configured. */
    FillRgb565(uai::camera_pipe2::driver::MainPipeFrameBuffer(), 0xf800U);
    FillRgb565(uai::camera_pipe2::driver::AncillaryPipeFrameBuffer(), 0x001fU);

    if (!uai::camera_pipe2::driver::IsOk(camera.Initialize())) {
        halt_with_message("camera_pipe2: camera initialization failed\n");
    }

    if (!uai::camera_pipe2::driver::IsOk(camera.Start())) {
        halt_with_message("camera_pipe2: camera start failed\n");
    }

    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "camera_pipe2: Pipe1/2 preview started\n")));

    tk_dly_tsk(1000);
    tm_printf(reinterpret_cast<const UB *>(
                  "camera_pipe2: pipe1_vsync=%u pipe2_frame=%u\n"),
              camera_pipe2_pipe1_vsync_count, camera_pipe2_pipe2_frame_count);

    for (;;) {
        if (!uai::camera_pipe2::driver::IsOk(camera.Process())) {
            halt_with_message("camera_pipe2: camera background process failed\n");
        }
        if (!uai::camera_pipe2::driver::IsOk(display.Process())) {
            halt_with_message("camera_pipe2: display process failed\n");
        }
        tk_dly_tsk(1);
    }
}
