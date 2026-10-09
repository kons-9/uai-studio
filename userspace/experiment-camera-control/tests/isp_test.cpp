#include "isp_camera.hpp"
#include <cstdlib>
#include <iostream>
#include <string>

extern "C" {
#include "isp_api.h"
ISP_HandleTypeDef hcamera_isp{};
}

namespace {
std::uint8_t auto_exposure = 1;
std::int32_t exposure = 1000;
std::int32_t gain = 0;
int writes = 0;
bool reject_gain = false;
ISP_StatAreaTypeDef area{0, 0, 2592, 1944};
ISP_ExposureCompTypeDef compensation{};
void Require(bool condition)
{
    if (!condition) {
        std::cerr << "ISP backend check failed\n";
        std::exit(1);
    }
}
ISP_StatusTypeDef GetExposure(
    std::uint32_t,
    std::int32_t *value
)
{
    *value = exposure;
    return ISP_OK;
}
ISP_StatusTypeDef GetGain(
    std::uint32_t,
    std::int32_t *value
)
{
    *value = gain;
    return ISP_OK;
}
ISP_StatusTypeDef SetExposure(
    std::uint32_t,
    std::int32_t value
)
{
    exposure = value;
    ++writes;
    return ISP_OK;
}
ISP_StatusTypeDef SetGain(
    std::uint32_t,
    std::int32_t value
)
{
    ++writes;
    if (reject_gain) {
        reject_gain = false;
        return ISP_ERR_SENSORGAIN;
    }
    gain = value;
    return ISP_OK;
}
}

extern "C" {
ISP_StatusTypeDef ISP_GetAECState(
    ISP_HandleTypeDef *,
    uint8_t *value
)
{
    *value = auto_exposure;
    return ISP_OK;
}
ISP_StatusTypeDef ISP_SetAECState(
    ISP_HandleTypeDef *,
    uint8_t value
)
{
    auto_exposure = value;
    return ISP_OK;
}
ISP_StatusTypeDef ISP_GetExposureTarget(
    ISP_HandleTypeDef *,
    ISP_ExposureCompTypeDef *value,
    uint32_t *target
)
{
    *value = compensation;
    *target = 56;
    return ISP_OK;
}
ISP_StatusTypeDef ISP_SetExposureTarget(
    ISP_HandleTypeDef *,
    ISP_ExposureCompTypeDef value
)
{
    compensation = value;
    return ISP_OK;
}
ISP_StatusTypeDef ISP_GetStatArea(
    ISP_HandleTypeDef *,
    ISP_StatAreaTypeDef *value
)
{
    *value = area;
    return ISP_OK;
}
ISP_StatusTypeDef ISP_SetStatArea(
    ISP_HandleTypeDef *,
    ISP_StatAreaTypeDef *value
)
{
    if (value->XSize < 4 || value->YSize < 4) {
        return ISP_ERR_STATAREA_EINVAL;
    }
    area = *value;
    return ISP_OK;
}
ISP_StatusTypeDef ISP_GetWBRefMode(
    ISP_HandleTypeDef *,
    uint8_t *automatic,
    uint32_t *
)
{
    *automatic = 1;
    return ISP_OK;
}
ISP_StatusTypeDef ISP_SetWBRefMode(
    ISP_HandleTypeDef *,
    uint8_t automatic,
    uint32_t temperature
)
{
    return automatic || temperature == 6500 ? ISP_OK : ISP_ERR_WB_COLORTEMP;
}
ISP_StatusTypeDef ISP_ListWBRefModes(
    ISP_HandleTypeDef *,
    uint32_t *values
)
{
    for (unsigned index = 0; index < ISP_AWB_COLORTEMP_REF; ++index) {
        values[index] = 2800 + index * 500;
    }
    return ISP_OK;
}
}

int main()
{
    experiment::camera::IspCamera camera;
    experiment::camera::State state;
    Require(camera.Read(state) == experiment::console::Status::kInvalidState);
    hcamera_isp.hDcmipp = &hcamera_isp;
    hcamera_isp.algorithm = &hcamera_isp;
    hcamera_isp.sensorInfo = {2592, 1944, 100, 30000, 0, 24000};
    hcamera_isp.appliHelpers = {GetExposure, GetGain, SetExposure, SetGain};
    Require(camera.Manual(12000, 3000) == experiment::console::Status::kInvalidState && writes == 0);
    Require(camera.AutoExposure(false) == experiment::console::Status::kOk && exposure == 1000 && writes == 0);
    Require(camera.Manual(30001, 0) == experiment::console::Status::kInvalidArgument && writes == 0);
    Require(camera.Manual(12000, 3000) == experiment::console::Status::kOk);
    reject_gain = true;
    Require(camera.Manual(5000, 5000) == experiment::console::Status::kHardware);
    Require(exposure == 12000 && gain == 3000);
    Require(camera.Read(state) == experiment::console::Status::kOk && state.reported_exposure_us == 12000);
    Require(camera.Compensation(5) == experiment::console::Status::kInvalidArgument);
    Require(camera.Compensation(-3) == experiment::console::Status::kOk && compensation == -3);
    Require(camera.Statistics({0, 0, 1, 1}) == experiment::console::Status::kInvalidArgument);
    Require(camera.Statistics({0, 0, 800, 480}) == experiment::console::Status::kOk && area.XSize == 800);
    Require(camera.WhiteBalance(1234) == experiment::console::Status::kInvalidArgument);
    Require(camera.WhiteBalance(6500) == experiment::console::Status::kOk);
    std::string output;
    Require(
        camera.ListWhiteBalance(
            {&output,
             [](void *context, const char *text, std::size_t count) {
                 static_cast<std::string *>(context)->append(text, count);
             }}
        )
        == experiment::console::Status::kOk
    );
    Require(output == "wb: auto 2800 3300 3800 4300 4800\n");
}
