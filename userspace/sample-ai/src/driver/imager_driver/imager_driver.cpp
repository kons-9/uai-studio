#include "driver/imager_driver/imager_driver.hpp"

namespace uai::ai {

using common::Error;
using common::ErrorCode;

Error ImagerDriver::Initialize(memory_manager::MemoryManager &memory,
                              memory_manager::MemoryHardware &memory_hardware)
{
    if (initialized_) {
        return {ErrorCode::kAlreadyInitialized, 0U, "camera.initialize"};
    }
    Error status = use_case_.Initialize(memory, memory_hardware);
    if (!status.Ok()) {
        return status;
    }
    memory_ = &memory;
    memory_hardware_ = &memory_hardware;
    initialized_ = true;
    return {ErrorCode::kOk, 0U, "camera.initialize"};
}

Error ImagerDriver::Start()
{
    if (!initialized_ || memory_ == nullptr) {
        return {ErrorCode::kNotInitialized, 0U, "camera.start"};
    }
    if (started_) {
        return {ErrorCode::kAlreadyInitialized, 0U, "camera.start"};
    }

    /* UseCase owns backend state and the DMA cache hand-off. */
    const Error status = use_case_.Start();
    if (!status.Ok()) {
        return status;
    }
    started_ = true;
    return {ErrorCode::kOk, 0U, "camera.start"};
}

Error ImagerDriver::Process()
{
    if (!initialized_ || !started_) {
        return {ErrorCode::kNotInitialized, 0U, "camera.process"};
    }
    return use_case_.Process();
}

Error ImagerDriver::Stop()
{
    if (!initialized_ || !started_) {
        return {ErrorCode::kNotInitialized, 0U, "camera.stop"};
    }
    const Error status = use_case_.Stop();
    if (status.Ok()) {
        started_ = false;
    }
    return status;
}

Error ImagerDriver::TakeCompletedCapture(
    memory_manager::CaptureFrame *frame)
{
    if (!initialized_ || memory_ == nullptr) {
        return {ErrorCode::kNotInitialized, 0U, "camera.take_capture"};
    }
    if (frame == nullptr) {
        return {ErrorCode::kInvalidArgument, 0U, "camera.take_capture"};
    }

    return use_case_.TakeCompletedCapture(frame);
}

} // namespace uai::ai
