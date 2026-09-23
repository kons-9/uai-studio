#include "camera_driver/camera_driver.hpp"

namespace uai::sample2 {

using common::Error;
using common::ErrorCode;

namespace {

Error FromBackend(uai::driver::DriverStatus status, const char *operation)
{
    if (uai::driver::IsOk(status)) {
        return {ErrorCode::kOk, 0U, operation};
    }
    return {ErrorCode::kHardware, static_cast<std::uint32_t>(status),
            operation};
}

} // namespace

Error CameraDriver::Initialize(memory_manager::MemoryManager &memory)
{
    if (initialized_) {
        return {ErrorCode::kAlreadyInitialized, 0U, "camera.initialize"};
    }
    std::uintptr_t first = 0U;
    std::uintptr_t second = 0U;
    if (!memory.CaptureBuffers(&first, &second).Ok()) {
        /* CaptureBuffers also verifies that the memory plan is initialized. */
        return {ErrorCode::kNotInitialized, 0U, "camera.initialize"};
    }

    const uai::driver::DriverStatus status = backend_.Initialize();
    if (!uai::driver::IsOk(status)) {
        return FromBackend(status, "camera.initialize");
    }
    memory_ = &memory;
    initialized_ = true;
    return {ErrorCode::kOk, 0U, "camera.initialize"};
}

Error CameraDriver::Start()
{
    if (!initialized_ || memory_ == nullptr) {
        return {ErrorCode::kNotInitialized, 0U, "camera.start"};
    }
    if (started_) {
        return {ErrorCode::kAlreadyInitialized, 0U, "camera.start"};
    }

    std::uintptr_t first = 0U;
    std::uintptr_t second = 0U;
    common::Error memory_status =
        memory_->CaptureBuffers(&first, &second);
    if (!memory_status.Ok()) {
        return memory_status;
    }

    /* Match the working ref application's DMA ownership hand-off.  The
     * capture buffers live in cacheable external PSRAM; invalidate them before
     * DCMIPP starts writing so the CPU cannot later read stale cache lines. */
    const memory_manager::Buffer first_buffer{
        first, memory_manager::kFrameBytes, 0U,
        memory_manager::Region::kCapture};
    const memory_manager::Buffer second_buffer{
        second, memory_manager::kFrameBytes, 1U,
        memory_manager::Region::kCapture};
    memory_status = memory_->PrepareForDmaWrite(first_buffer);
    if (!memory_status.Ok()) {
        return memory_status;
    }
    memory_status = memory_->PrepareForDmaWrite(second_buffer);
    if (!memory_status.Ok()) {
        return memory_status;
    }

    const uai::driver::DriverStatus status = backend_.Start(first, second);
    if (!uai::driver::IsOk(status)) {
        return FromBackend(status, "camera.start");
    }
    started_ = true;
    return {ErrorCode::kOk, 0U, "camera.start"};
}

Error CameraDriver::Process()
{
    if (!initialized_ || !started_) {
        return {ErrorCode::kNotInitialized, 0U, "camera.process"};
    }
    return FromBackend(backend_.Process(), "camera.process");
}

Error CameraDriver::TakeCompletedCapture(
    memory_manager::CaptureFrame *frame)
{
    if (!initialized_ || !started_ || memory_ == nullptr) {
        return {ErrorCode::kNotInitialized, 0U, "camera.take_capture"};
    }
    if (frame == nullptr) {
        return {ErrorCode::kInvalidArgument, 0U, "camera.take_capture"};
    }

    const std::uintptr_t address = backend_.TakeCompletedFrame();
    if (address == 0U) {
        return {ErrorCode::kNoFrame, 0U, "camera.take_capture"};
    }
    return memory_->ImportCompletedCapture(address, frame);
}

} // namespace uai::sample2
