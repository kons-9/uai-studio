#include "driver/touch_driver/touch_driver.hpp"

namespace uai::ai::touch {
namespace {

constexpr std::uint16_t kTouchWidth = 800U;
constexpr std::uint16_t kTouchHeight = 480U;

} // namespace

common::Error TouchDriver::Initialize()
{
    common::Error management_status = management_->Initialize();
    if (!management_status.Ok() && management_status.Code() != common::ErrorCode::kAlreadyInitialized) {
        return management_status;
    }
    Writer writer;
    management_status = management_->Acquire(&writer);
    if (!management_status.Ok())
        return management_status;
    if (initialized_)
        return {common::ErrorCode::kAlreadyInitialized};

    auto status = registers_.Initialize();
    if (!status.Ok())
        return status;
    initialized_ = true;
    return {common::ErrorCode::kOk};
}

common::Error TouchDriver::Read(
    ui::TouchPoint *sample,
    const Writer &writer
)
{
    auto status = ReadRaw(sample, writer);
    if (status.Ok() && sample->active) {
        sample->x = sample->x < kTouchWidth ? sample->x : kTouchWidth - 1U;
        sample->y = sample->y < kTouchHeight ? sample->y : kTouchHeight - 1U;
    }
    return status;
}

common::Error TouchDriver::ReadRaw(
    ui::TouchPoint *sample,
    const Writer &writer
)
{
    common::Error ownership = management_->Validate(writer);
    if (!ownership.Ok())
        return ownership;
    if (!initialized_) {
        return {common::ErrorCode::kNotInitialized};
    }
    if (sample == nullptr) {
        return {common::ErrorCode::kInvalidArgument};
    }

    return registers_.Read(*sample);
}

} // namespace uai::ai::touch
