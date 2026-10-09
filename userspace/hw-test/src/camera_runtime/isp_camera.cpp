#include "isp_camera.hpp"

extern "C" {
#include "isp_api.h"
extern ISP_HandleTypeDef hcamera_isp;
}

namespace experiment::camera {

namespace {

console::Status Convert(ISP_StatusTypeDef status)
{
    if (status == ISP_OK) {
        return console::Status::kOk;
    }
    if (status == ISP_ERR_STATAREA_EINVAL || status == ISP_ERR_WB_COLORTEMP) {
        return console::Status::kInvalidArgument;
    }
    return console::Status::kHardware;
}

bool IsInitialized()
{
    /* STM32Cube N6's ISP handle has no isInitialized member. ISP_Init sets
     * these fields on success, while ISP_DeInit clears the handle. */
    return hcamera_isp.hDcmipp != nullptr && hcamera_isp.algorithm != nullptr && hcamera_isp.sensorInfo.width != 0U
        && hcamera_isp.sensorInfo.height != 0U;
}

}

console::Status IspCamera::Read(State &state)
{
    if (!IsInitialized()) {
        return console::Status::kInvalidState;
    }
    auto &helpers = hcamera_isp.appliHelpers;
    if (!helpers.GetSensorExposure || !helpers.GetSensorGain) {
        return console::Status::kHardware;
    }
    State next;
    std::uint8_t automatic = 0;
    std::uint8_t white_balance = 0;
    ISP_ExposureCompTypeDef compensation{};
    ISP_StatAreaTypeDef area{};
    if (ISP_GetAECState(&hcamera_isp, &automatic) != ISP_OK
        || ISP_GetExposureTarget(&hcamera_isp, &compensation, &next.target) != ISP_OK
        || ISP_GetStatArea(&hcamera_isp, &area) != ISP_OK
        || ISP_GetWBRefMode(&hcamera_isp, &white_balance, &next.color_temperature) != ISP_OK
        || helpers.GetSensorExposure(hcamera_isp.cameraInstance, &next.reported_exposure_us) != ISP_OK
        || helpers.GetSensorGain(hcamera_isp.cameraInstance, &next.reported_gain_mdB) != ISP_OK) {
        return console::Status::kHardware;
    }
    next.auto_exposure = automatic != 0;
    next.auto_white_balance = white_balance != 0;
    next.compensation = static_cast<int>(compensation);
    next.statistics = {area.X0, area.Y0, area.XSize, area.YSize};
    next.sensor_width = hcamera_isp.sensorInfo.width;
    next.sensor_height = hcamera_isp.sensorInfo.height;
    state = next;
    return console::Status::kOk;
}

console::Status IspCamera::AutoExposure(bool enabled)
{
    return IsInitialized() ? Convert(ISP_SetAECState(&hcamera_isp, enabled ? 1 : 0)) : console::Status::kInvalidState;
}

console::Status IspCamera::Compensation(int half_stops)
{
    if (half_stops < -4 || half_stops > 4) {
        return console::Status::kInvalidArgument;
    }
    return IsInitialized()
        ? Convert(ISP_SetExposureTarget(&hcamera_isp, static_cast<ISP_ExposureCompTypeDef>(half_stops)))
        : console::Status::kInvalidState;
}

console::Status IspCamera::Manual(
    std::int32_t exposure_us,
    std::int32_t gain_mdB
)
{
    State previous;
    const auto read = Read(previous);
    if (read != console::Status::kOk) {
        return read;
    }
    if (previous.auto_exposure) {
        return console::Status::kInvalidState;
    }
    const auto &info = hcamera_isp.sensorInfo;
    if (exposure_us < 0 || gain_mdB < 0 || static_cast<std::uint32_t>(exposure_us) < info.exposure_min
        || static_cast<std::uint32_t>(exposure_us) > info.exposure_max
        || static_cast<std::uint32_t>(gain_mdB) < info.gain_min
        || static_cast<std::uint32_t>(gain_mdB) > info.gain_max) {
        return console::Status::kInvalidArgument;
    }
    auto &helpers = hcamera_isp.appliHelpers;
    if (!helpers.SetSensorExposure || !helpers.SetSensorGain) {
        return console::Status::kHardware;
    }
    const auto instance = hcamera_isp.cameraInstance;
    if (helpers.SetSensorExposure(instance, exposure_us) != ISP_OK
        || helpers.SetSensorGain(instance, gain_mdB) != ISP_OK) {
        helpers.SetSensorExposure(instance, previous.reported_exposure_us);
        helpers.SetSensorGain(instance, previous.reported_gain_mdB);
        return console::Status::kHardware;
    }
    return console::Status::kOk;
}

console::Status IspCamera::Statistics(Rect rectangle)
{
    if (!IsInitialized()) {
        return console::Status::kInvalidState;
    }
    if (!Inside(rectangle, hcamera_isp.sensorInfo.width, hcamera_isp.sensorInfo.height)) {
        return console::Status::kInvalidArgument;
    }
    ISP_StatAreaTypeDef area{rectangle.x, rectangle.y, rectangle.width, rectangle.height};
    return Convert(ISP_SetStatArea(&hcamera_isp, &area));
}

console::Status IspCamera::WhiteBalance(std::uint32_t temperature)
{
    return IsInitialized() ? Convert(ISP_SetWBRefMode(&hcamera_isp, temperature == 0 ? 1 : 0, temperature))
                           : console::Status::kInvalidState;
}

console::Status IspCamera::ListWhiteBalance(const console::Writer &writer)
{
    if (!IsInitialized()) {
        return console::Status::kInvalidState;
    }
    std::uint32_t temperatures[ISP_AWB_COLORTEMP_REF]{};
    const auto status = Convert(ISP_ListWBRefModes(&hcamera_isp, temperatures));
    if (status != console::Status::kOk) {
        return status;
    }
    writer.Write("wb: auto");
    for (auto temperature : temperatures) {
        char text[24];
        std::snprintf(text, sizeof(text), " %lu", static_cast<unsigned long>(temperature));
        writer.Write(text);
    }
    writer.Write("\n");
    return console::Status::kOk;
}

}
