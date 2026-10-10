#include "driver/lcd_driver/display_driver.hpp"
#include "driver/cache_driver/cache_driver.hpp"

#include <limits>

namespace uai::ai::lcd {
namespace {

bool Valid(const buffer::Buffer &frame)
{
    return frame.region == buffer::Region::kDisplay && frame.address != 0U && frame.address % 32U == 0U
        && frame.size == kDisplayBytes && frame.address <= std::numeric_limits<std::uintptr_t>::max() - frame.size;
}

common::Error Convert(uai::driver::DriverStatus status)
{
    if (uai::driver::IsOk(status))
        return {};
    if (status == uai::driver::DriverStatus::kBusy)
        return {common::ErrorCode::kNoBuffer};
    return {common::ErrorCode::kHardware};
}

}

common::Error DisplayDriver::Initialize(
    const buffer::Buffer &initial,
    const Writer &writer
)
{
    auto status = management_->Validate(writer);
    if (!status.Ok())
        return status;
    if (initialized_)
        return {common::ErrorCode::kAlreadyInitialized};
    if (!Valid(initial))
        return {common::ErrorCode::kInvalidArgument};
    status = cache::CacheDriver::Clean(reinterpret_cast<void *>(initial.address), initial.size);
    if (!status.Ok())
        return status;
    status = Convert(registers_.Initialize(initial.address));
    if (status.Ok())
        initialized_ = true;
    return status;
}

common::Error DisplayDriver::Synchronize(const Writer &writer)
{
    auto status = management_->Validate(writer);
    if (!status.Ok())
        return status;
    if (!initialized_)
        return {common::ErrorCode::kNotInitialized};
    return Convert(registers_.Synchronize());
}

common::Error DisplayDriver::Present(
    const buffer::Buffer &frame,
    const Writer &writer
)
{
    auto status = management_->Validate(writer);
    if (!status.Ok())
        return status;
    if (!initialized_)
        return {common::ErrorCode::kNotInitialized};
    if (!Valid(frame))
        return {common::ErrorCode::kInvalidArgument};
    status = Convert(registers_.Synchronize());
    if (!status.Ok())
        return status;
    status = cache::CacheDriver::Clean(reinterpret_cast<void *>(frame.address), frame.size);
    return status.Ok() ? Convert(registers_.Present(frame.address)) : status;
}

}