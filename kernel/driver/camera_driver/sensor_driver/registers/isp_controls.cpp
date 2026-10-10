#include "driver/camera_driver/sensor_driver/registers/isp_controls.hpp"

extern "C" {
#include "isp_api.h"
extern ISP_HandleTypeDef hcamera_isp;
}

namespace uai::ai::camera::sensor::registers {
namespace {

bool Ready()
{
    return hcamera_isp.hDcmipp != nullptr && hcamera_isp.algorithm != nullptr && hcamera_isp.sensorInfo.width != 0
        && hcamera_isp.sensorInfo.height != 0;
}

common::Error Convert(ISP_StatusTypeDef status)
{
    if (status == ISP_OK)
        return {};
    if (status == ISP_ERR_STATAREA_EINVAL || status == ISP_ERR_WB_COLORTEMP || status == ISP_ERR_EINVAL)
        return {common::ErrorCode::kInvalidArgument};
    return {common::ErrorCode::kHardware};
}

common::Error ApplySettings(const State &state)
{
    auto status = IspControls::AutoExposure(false);
    if (status.Ok())
        status = IspControls::Compensation(state.compensation);
    if (status.Ok())
        status = IspControls::Statistics(state.statistics);
    if (status.Ok())
        status = IspControls::WhiteBalance(state.auto_white_balance ? 0 : state.color_temperature);
    if (status.Ok() && state.reported_exposure_us > 0)
        status = IspControls::Manual(state.reported_exposure_us, state.reported_gain_mdB);
    if (status.Ok())
        status = IspControls::AutoExposure(state.auto_exposure);
    return status;
}

}

common::Error IspControls::Read(State *state)
{
    if (state == nullptr)
        return {common::ErrorCode::kInvalidArgument};
    if (!Ready())
        return {common::ErrorCode::kNotInitialized};
    auto &helpers = hcamera_isp.appliHelpers;
    if (!helpers.GetSensorExposure || !helpers.GetSensorGain)
        return {common::ErrorCode::kHardware};
    State next;
    std::uint8_t automatic = 0, white_balance = 0;
    ISP_ExposureCompTypeDef compensation{};
    ISP_StatAreaTypeDef area{};
    if (ISP_GetAECState(&hcamera_isp, &automatic) != ISP_OK
        || ISP_GetExposureTarget(&hcamera_isp, &compensation, &next.target) != ISP_OK
        || ISP_GetStatArea(&hcamera_isp, &area) != ISP_OK
        || ISP_GetWBRefMode(&hcamera_isp, &white_balance, &next.color_temperature) != ISP_OK
        || helpers.GetSensorExposure(hcamera_isp.cameraInstance, &next.reported_exposure_us) != ISP_OK
        || helpers.GetSensorGain(hcamera_isp.cameraInstance, &next.reported_gain_mdB) != ISP_OK) {
        return {common::ErrorCode::kHardware};
    }
    next.auto_exposure = automatic != 0;
    next.auto_white_balance = white_balance != 0;
    next.compensation = static_cast<int>(compensation);
    next.statistics = {area.X0, area.Y0, area.XSize, area.YSize};
    next.sensor_width = hcamera_isp.sensorInfo.width;
    next.sensor_height = hcamera_isp.sensorInfo.height;
    *state = next;
    return {};
}

common::Error IspControls::AutoExposure(bool enabled)
{
    return Ready() ? Convert(ISP_SetAECState(&hcamera_isp, enabled ? 1 : 0))
                   : common::Error{common::ErrorCode::kNotInitialized};
}

common::Error IspControls::Compensation(int half_stops)
{
    if (half_stops < -4 || half_stops > 4)
        return {common::ErrorCode::kInvalidArgument};
    return Ready() ? Convert(ISP_SetExposureTarget(&hcamera_isp, static_cast<ISP_ExposureCompTypeDef>(half_stops)))
                   : common::Error{common::ErrorCode::kNotInitialized};
}

common::Error IspControls::Manual(
    std::int32_t exposure_us,
    std::int32_t gain_mdB
)
{
    State previous;
    auto status = Read(&previous);
    if (!status.Ok())
        return status;
    if (previous.auto_exposure)
        return {common::ErrorCode::kInvalidState};
    const auto &info = hcamera_isp.sensorInfo;
    if (exposure_us < 0 || gain_mdB < 0 || static_cast<std::uint32_t>(exposure_us) < info.exposure_min
        || static_cast<std::uint32_t>(exposure_us) > info.exposure_max
        || static_cast<std::uint32_t>(gain_mdB) < info.gain_min
        || static_cast<std::uint32_t>(gain_mdB) > info.gain_max) {
        return {common::ErrorCode::kInvalidArgument};
    }
    auto &helpers = hcamera_isp.appliHelpers;
    if (!helpers.SetSensorExposure || !helpers.SetSensorGain)
        return {common::ErrorCode::kHardware};
    const auto instance = hcamera_isp.cameraInstance;
    if (helpers.SetSensorExposure(instance, exposure_us) != ISP_OK
        || helpers.SetSensorGain(instance, gain_mdB) != ISP_OK) {
        helpers.SetSensorExposure(instance, previous.reported_exposure_us);
        helpers.SetSensorGain(instance, previous.reported_gain_mdB);
        return {common::ErrorCode::kHardware};
    }
    return {};
}

common::Error IspControls::Statistics(Rect rectangle)
{
    if (!Ready())
        return {common::ErrorCode::kNotInitialized};
    if (!Inside(rectangle, hcamera_isp.sensorInfo.width, hcamera_isp.sensorInfo.height))
        return {common::ErrorCode::kInvalidArgument};
    ISP_StatAreaTypeDef area{rectangle.x, rectangle.y, rectangle.width, rectangle.height};
    return Convert(ISP_SetStatArea(&hcamera_isp, &area));
}

common::Error IspControls::WhiteBalance(std::uint32_t temperature)
{
    return Ready() ? Convert(ISP_SetWBRefMode(&hcamera_isp, temperature == 0 ? 1 : 0, temperature))
                   : common::Error{common::ErrorCode::kNotInitialized};
}

common::Error IspControls::ListWhiteBalance(
    std::uint32_t *temperatures,
    std::size_t capacity,
    std::size_t *count
)
{
    if (temperatures == nullptr || count == nullptr || capacity < ISP_AWB_COLORTEMP_REF)
        return {common::ErrorCode::kInvalidArgument};
    if (!Ready())
        return {common::ErrorCode::kNotInitialized};
    std::uint32_t modes[ISP_AWB_COLORTEMP_REF]{};
    auto status = Convert(ISP_ListWBRefModes(&hcamera_isp, modes));
    if (!status.Ok())
        return status;
    for (std::size_t index = 0; index < ISP_AWB_COLORTEMP_REF; ++index)
        temperatures[index] = modes[index];
    *count = ISP_AWB_COLORTEMP_REF;
    return {};
}

common::Error IspControls::Apply(const State &state)
{
    State previous;
    auto status = Read(&previous);
    if (!status.Ok())
        return status;
    status = ApplySettings(state);
    if (!status.Ok())
        (void)ApplySettings(previous);
    return status;
}

}