#include <tk/tkernel.h>

#include "driver/camera_driver.hpp"
#include "driver/display_driver.hpp"

extern "C" {
#include <tm/tmonitor.h>
}

namespace {

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
        "sample1: initializing LCD and IMX335 camera\n")));

    uai::driver::DisplayDriver display;
    uai::driver::CameraDriver camera;

    if (!uai::driver::IsOk(display.Initialize())) {
        halt_with_message("sample1: display initialization failed\n");
    }

    if (!uai::driver::IsOk(camera.Initialize())) {
        halt_with_message("sample1: camera initialization failed\n");
    }

    if (!uai::driver::IsOk(camera.Start())) {
        halt_with_message("sample1: camera start failed\n");
    }

    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "sample1: camera preview started\n")));

    for (;;) {
        if (!uai::driver::IsOk(camera.Process())) {
            halt_with_message("sample1: camera background process failed\n");
        }
        if (!uai::driver::IsOk(display.Process())) {
            halt_with_message("sample1: display process failed\n");
        }
        tk_dly_tsk(1);
    }
}
