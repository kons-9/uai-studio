#include "driver/camera_driver/camera_driver.hpp"
#include "driver/camera_driver/sensor_driver/registers/isp_controls.hpp"

namespace uai::ai::camera {

common::Error CameraDriver::ValidateControl(const Writer &writer) const
{
    auto status = management_->Validate(writer);
    if (!status.Ok())
        return status;
    return initialized_ ? common::Error{} : common::Error{common::ErrorCode::kNotInitialized};
}

common::Error CameraDriver::ReadState(
    State *state,
    const Writer &writer
) const
{
    auto status = ValidateControl(writer);
    if (status.Ok())
        status = sensor::registers::IspControls::Read(state);
    if (status.Ok()) {
        saved_controls_ = *state;
        have_saved_controls_ = true;
    }
    return status;
}

common::Error CameraDriver::ApplyState(
    const State &state,
    const Writer &writer
)
{
    auto status = ValidateControl(writer);
    if (status.Ok())
        status = sensor::registers::IspControls::Apply(state);
    if (status.Ok())
        status = ReadState(&saved_controls_, writer);
    return status;
}

common::Error CameraDriver::AutoExposure(
    bool enabled,
    const Writer &writer
)
{
    auto status = ValidateControl(writer);
    return status.Ok() ? sensor::registers::IspControls::AutoExposure(enabled) : status;
}

common::Error CameraDriver::Compensation(
    int half_stops,
    const Writer &writer
)
{
    auto status = ValidateControl(writer);
    return status.Ok() ? sensor::registers::IspControls::Compensation(half_stops) : status;
}

common::Error CameraDriver::Manual(
    std::int32_t exposure_us,
    std::int32_t gain_mdB,
    const Writer &writer
)
{
    auto status = ValidateControl(writer);
    return status.Ok() ? sensor::registers::IspControls::Manual(exposure_us, gain_mdB) : status;
}

common::Error CameraDriver::Statistics(
    Rect rectangle,
    const Writer &writer
)
{
    auto status = ValidateControl(writer);
    return status.Ok() ? sensor::registers::IspControls::Statistics(rectangle) : status;
}

common::Error CameraDriver::WhiteBalance(
    std::uint32_t temperature,
    const Writer &writer
)
{
    auto status = ValidateControl(writer);
    return status.Ok() ? sensor::registers::IspControls::WhiteBalance(temperature) : status;
}

common::Error CameraDriver::ListWhiteBalance(
    std::uint32_t *temperatures,
    std::size_t capacity,
    std::size_t *count,
    const Writer &writer
) const
{
    auto status = ValidateControl(writer);
    return status.Ok() ? sensor::registers::IspControls::ListWhiteBalance(temperatures, capacity, count) : status;
}

common::Error CameraDriver::GetGeometry(
    Geometry *geometry,
    const Writer &writer
) const
{
    auto status = ValidateControl(writer);
    if (!status.Ok())
        return status;
    if (geometry == nullptr)
        return {common::ErrorCode::kInvalidArgument};
    *geometry = capture_.geometry;
    return {};
}

}