#include "driver/console_driver/console_driver.hpp"

namespace uai::ai::console {

common::Error ConsoleDriver::Initialize(
    Notifier notifier,
    const Writer &writer
)
{
    auto status = management_->Validate(writer);
    if (!status.Ok())
        return status;
    if (initialized_)
        return {common::ErrorCode::kAlreadyInitialized};
    status = registers_.Initialize(notifier);
    if (status.Ok())
        initialized_ = true;
    return status;
}
common::Error ConsoleDriver::Read(
    Input *input,
    const Writer &writer
)
{
    auto status = management_->Validate(writer);
    if (!status.Ok())
        return status;
    if (!initialized_)
        return {common::ErrorCode::kNotInitialized};
    if (input == nullptr)
        return {common::ErrorCode::kInvalidArgument};
    return registers_.Read(*input);
}
common::Error ConsoleDriver::Write(
    const char *text,
    std::size_t size,
    const Writer &writer
)
{
    auto status = management_->Validate(writer);
    if (!status.Ok())
        return status;
    if (!initialized_)
        return {common::ErrorCode::kNotInitialized};
    if (text == nullptr && size != 0)
        return {common::ErrorCode::kInvalidArgument};
    return registers_.Write(text, size);
}

}