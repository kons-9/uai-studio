#include "driver/lcd_driver/lcd_driver.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace uai::sample2 {

using common::Error;
using common::ErrorCode;

namespace {

constexpr std::uint16_t kRed = 0xF800U;
constexpr std::int32_t kLineWidth = 4;
constexpr std::uint16_t kInitialPattern[] = {
    0xFFFFU, 0xFFE0U, 0x07FFU, 0x07E0U,
    0xF81FU, 0xF800U, 0x001FU, 0x0000U,
};

bool InRange(std::int32_t value, std::int32_t limit)
{
    return value >= 0 && value < limit;
}

} // namespace

Error LcdDriver::FromBackend(uai::driver::DriverStatus status,
                             const char *operation)
{
    if (uai::driver::IsOk(status)) {
        return {ErrorCode::kOk, 0U, operation};
    }
    return {ErrorCode::kHardware, static_cast<std::uint32_t>(status),
            operation};
}

Error LcdDriver::Initialize(memory_manager::MemoryManager &memory,
                            memory_manager::MemoryHardware &memory_hardware)
{
    if (initialized_) {
        return {ErrorCode::kAlreadyInitialized, 0U, "lcd.initialize"};
    }
    memory_ = &memory;
    memory_hardware_ = &memory_hardware;
    const uai::driver::DriverStatus status = backend_.Initialize();
    if (!uai::driver::IsOk(status)) {
        memory_ = nullptr;
        return FromBackend(status, "lcd.initialize");
    }
    initialized_ = true;
    return {ErrorCode::kOk, 0U, "lcd.initialize"};
}

void LcdDriver::FillInitialFrame(const memory_manager::DisplayBuffer &buffer,
                                 const memory_manager::BoxSet &boxes)
{
    auto *pixels = reinterpret_cast<std::uint16_t *>(buffer.buffer.address);
    for (std::size_t y = 0U; y < memory_manager::kFrameHeight; ++y) {
        for (std::size_t x = 0U; x < memory_manager::kFrameWidth; ++x) {
            const std::size_t color =
                x * (sizeof(kInitialPattern) / sizeof(kInitialPattern[0])) /
                memory_manager::kFrameWidth;
            pixels[y * memory_manager::kFrameWidth + x] =
                kInitialPattern[color];
        }
    }
    DrawBoxes(buffer, boxes);
}

void LcdDriver::DrawBoxes(const memory_manager::DisplayBuffer &buffer,
                          const memory_manager::BoxSet &boxes)
{
    auto *pixels = reinterpret_cast<std::uint16_t *>(buffer.buffer.address);
    const std::int32_t width =
        static_cast<std::int32_t>(memory_manager::kFrameWidth);
    const std::int32_t height =
        static_cast<std::int32_t>(memory_manager::kFrameHeight);
    const std::uint32_t count =
        boxes.count < memory_manager::kMaxBoxes ? boxes.count
                                                : memory_manager::kMaxBoxes;

    for (std::uint32_t i = 0U; i < count; ++i) {
        const memory_manager::Box &box = boxes.boxes[i];
        const std::int32_t left = box.x;
        const std::int32_t top = box.y;
        const std::int32_t right = left + box.width - 1;
        const std::int32_t bottom = top + box.height - 1;

        for (std::int32_t thickness = 0; thickness < kLineWidth; ++thickness) {
            const std::int32_t y_top = top + thickness;
            const std::int32_t y_bottom = bottom - thickness;
            const std::int32_t x_left = left + thickness;
            const std::int32_t x_right = right - thickness;

            for (std::int32_t x = left; x <= right; ++x) {
                if (InRange(x, width) && InRange(y_top, height)) {
                    pixels[y_top * width + x] = kRed;
                }
                if (InRange(x, width) && InRange(y_bottom, height)) {
                    pixels[y_bottom * width + x] = kRed;
                }
            }
            for (std::int32_t y = top; y <= bottom; ++y) {
                if (InRange(x_left, width) && InRange(y, height)) {
                    pixels[y * width + x_left] = kRed;
                }
                if (InRange(x_right, width) && InRange(y, height)) {
                    pixels[y * width + x_right] = kRed;
                }
            }
        }
    }
}

Error LcdDriver::ShowInitialFrame(
    const memory_manager::BoxSet &boxes)
{
    if (!initialized_ || memory_ == nullptr) {
        return {ErrorCode::kNotInitialized, 0U, "lcd.show_initial"};
    }

    memory_manager::DisplayBuffer first{};
    Error status = memory_->AcquireDisplayBuffer(&first);
    if (!status.Ok()) {
        return status;
    }
    FillInitialFrame(first, boxes);
    status = memory_hardware_->PrepareForPeripheralRead(first.buffer);
    if (!status.Ok()) {
        (void)memory_->ReleaseDisplayBuffer(first);
        return status;
    }
    const uai::driver::DriverStatus backend_status =
        backend_.Process(first.buffer.address);
    if (!uai::driver::IsOk(backend_status)) {
        (void)memory_->ReleaseDisplayBuffer(first);
        return FromBackend(backend_status, "lcd.show_initial");
    }
    status = memory_->CommitDisplayBuffer(first);
    if (!status.Ok()) {
        (void)memory_->ReleaseDisplayBuffer(first);
        return status;
    }

    /* Both B surfaces start with a valid initial frame. The second one remains
     * free and will be overwritten as a whole on the first camera frame. */
    memory_manager::DisplayBuffer spare{};
    status = memory_->AcquireDisplayBuffer(&spare);
    if (status.Ok()) {
        FillInitialFrame(spare, boxes);
    status = memory_hardware_->PrepareForPeripheralRead(spare.buffer);
        if (status.Ok()) {
            status = memory_->ReleaseDisplayBuffer(spare);
        } else {
            (void)memory_->ReleaseDisplayBuffer(spare);
        }
    }
    return status.Ok() ? Error{ErrorCode::kOk, 0U, "lcd.show_initial"}
                       : status;
}

Error LcdDriver::ComposeAndPresent(
    const memory_manager::CaptureFrame &capture,
    const memory_manager::BoxSet &boxes)
{
    if (!initialized_ || memory_ == nullptr) {
        return {ErrorCode::kNotInitialized, 0U, "lcd.compose"};
    }
    if (!capture) {
        return {ErrorCode::kInvalidArgument, 0U, "lcd.compose"};
    }

    Error status = memory_->ValidateCaptureFrame(capture);
    if (!status.Ok()) {
        return status;
    }
    status = memory_hardware_->PrepareForCpuRead(capture.buffer);
    if (!status.Ok()) {
        return status;
    }

    memory_manager::DisplayBuffer display{};
    status = memory_->AcquireDisplayBuffer(&display);
    if (!status.Ok()) {
        return status;
    }
    std::memcpy(reinterpret_cast<void *>(display.buffer.address),
                reinterpret_cast<const void *>(capture.buffer.address),
                memory_manager::kFrameBytes);
    DrawBoxes(display, boxes);

    status = memory_hardware_->PrepareForPeripheralRead(display.buffer);
    if (!status.Ok()) {
        (void)memory_->ReleaseDisplayBuffer(display);
        return status;
    }
    const uai::driver::DriverStatus backend_status =
        backend_.Process(display.buffer.address);
    if (!uai::driver::IsOk(backend_status)) {
        (void)memory_->ReleaseDisplayBuffer(display);
        return FromBackend(backend_status, "lcd.present");
    }
    return memory_->CommitDisplayBuffer(display);
}

} // namespace uai::sample2
