#include "driver/lcd_driver/lcd_driver.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" {
#include <tm/tmonitor.h>
}

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

std::uint16_t CoordinatePatternPixel(std::size_t x, std::size_t y)
{
    std::uint16_t color = (y / 10U) % 2U == 0U ? 0x0841U : 0x2104U;
    if ((x % 40U) < 2U) {
        color = 0x07FFU;
    }
    if ((y % 10U) == 0U) {
        color = 0xFFFFU;
    }
    if ((y % 20U) == 0U) {
        color = 0xFFE0U;
    }
    if ((y % 40U) == 0U) {
        color = 0xF800U;
    }
    if (x >= 4U && x < 40U && (y % 10U) >= 3U && (y % 10U) < 8U) {
        const std::size_t bit = (x - 4U) / 6U;
        const std::size_t in_bit = (x - 4U) % 6U;
        if (bit < 6U && in_bit < 4U && (((y / 10U) >> bit) & 1U) != 0U) {
            color = 0x07E0U;
        }
    }
    if (y < 20U && (x % 20U) == 0U) {
        color = 0xF81FU;
    }
    return color;
}

std::uint32_t Crc32(const std::uint8_t *bytes, std::size_t size)
{
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0U; i < size; ++i) {
        crc ^= bytes[i];
        for (std::uint32_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xEDB88320U : 0U);
        }
    }
    return ~crc;
}

} // namespace

Error LcdDriver::FromBackend(uai::driver::DriverStatus status,
                             const char *operation)
{
    if (uai::driver::IsOk(status)) {
        return {ErrorCode::kOk, 0U, operation};
    }
    if (status == uai::driver::DriverStatus::kBusy) {
        return {ErrorCode::kNoBuffer, 0U, operation};
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
                                 const memory_manager::BoxSet &boxes,
                                 bool coordinate_pattern)
{
    auto *pixels = reinterpret_cast<std::uint16_t *>(buffer.buffer.address);
    for (std::size_t y = 0U; y < memory_manager::kFrameHeight; ++y) {
        for (std::size_t x = 0U; x < memory_manager::kFrameWidth; ++x) {
            if (coordinate_pattern) {
                pixels[y * memory_manager::kFrameWidth + x] =
                    CoordinatePatternPixel(x, y);
                continue;
            }
            const std::size_t color =
                x * (sizeof(kInitialPattern) / sizeof(kInitialPattern[0])) /
                memory_manager::kFrameWidth;
            pixels[y * memory_manager::kFrameWidth + x] =
                kInitialPattern[color];
        }
    }
    DrawBoxes(buffer, boxes);
}

Error LcdDriver::GenerateCoordinatePattern(
    const memory_manager::Buffer &destination) const
{
    if (!initialized_ || destination.region != memory_manager::Region::kCapture ||
        destination.size != memory_manager::kFrameBytes ||
        destination.address == 0U) {
        return {ErrorCode::kInvalidArgument, 0U,
                "lcd.generate_coordinate_pattern"};
    }
    auto *pixels = reinterpret_cast<std::uint16_t *>(destination.address);
    for (std::size_t y = 0U; y < memory_manager::kFrameHeight; ++y) {
        for (std::size_t x = 0U; x < memory_manager::kFrameWidth; ++x) {
            pixels[y * memory_manager::kFrameWidth + x] =
                CoordinatePatternPixel(x, y);
        }
    }
    return {ErrorCode::kOk, 0U, "lcd.generate_coordinate_pattern"};
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
    const memory_manager::BoxSet &boxes,
    bool coordinate_pattern)
{
    if (!initialized_ || memory_ == nullptr) {
        return {ErrorCode::kNotInitialized, 0U, "lcd.show_initial"};
    }

    memory_manager::DisplayBuffer first{};
    Error status = memory_->AcquireDisplayBuffer(&first);
    if (!status.Ok()) {
        return status;
    }
    FillInitialFrame(first, boxes, coordinate_pattern);
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
        FillInitialFrame(spare, boxes, coordinate_pattern);
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

Error LcdDriver::SynchronizeCurrentFrame()
{
    if (!initialized_ || memory_ == nullptr) {
        return {ErrorCode::kNotInitialized, 0U,
                "lcd.synchronize_current_frame"};
    }
    const uai::driver::DriverStatus status = backend_.Synchronize();
    if (!uai::driver::IsOk(status)) {
        return FromBackend(status, "lcd.synchronize_current_frame");
    }
    return memory_->CompleteDisplayHandoff();
}

Error LcdDriver::ComposeAndPresent(
    const memory_manager::CaptureFrame &capture,
    const memory_manager::BoxSet &boxes,
    bool log_copy_crc)
{
    if (!initialized_ || memory_ == nullptr) {
        return {ErrorCode::kNotInitialized, 0U, "lcd.compose"};
    }
    if (!capture) {
        return {ErrorCode::kInvalidArgument, 0U, "lcd.compose"};
    }
    Error status{};

    /* Do not reacquire the previous active surface until LTDC confirms that
     * the queued VBlank reload has latched the new CFBAR. */
    const uai::driver::DriverStatus sync_status = backend_.Synchronize();
    if (sync_status == uai::driver::DriverStatus::kBusy) {
        return {ErrorCode::kNoBuffer, 0U, "lcd.reload.pending"};
    }
    if (!uai::driver::IsOk(sync_status)) {
        return FromBackend(sync_status, "lcd.reload.wait");
    }
    status = memory_->CompleteDisplayHandoff();
    if (!status.Ok()) {
        return status;
    }

    status = memory_->ValidateCaptureFrame(capture);
    if (!status.Ok()) {
        return status;
    }
    status = memory_hardware_->PrepareForCpuRead(capture.buffer);
    if (!status.Ok()) {
        return status;
    }
    const auto *source_bytes = reinterpret_cast<const std::uint8_t *>(
        capture.buffer.address);
    const std::uint32_t source_crc =
        log_copy_crc ? Crc32(source_bytes, memory_manager::kFrameBytes) : 0U;

    memory_manager::DisplayBuffer display{};
    status = memory_->AcquireDisplayBuffer(&display);
    if (!status.Ok()) {
        return status;
    }
    std::memcpy(reinterpret_cast<void *>(display.buffer.address),
                reinterpret_cast<const void *>(capture.buffer.address),
                memory_manager::kFrameBytes);
    const std::uint32_t copied_crc =
        log_copy_crc
            ? Crc32(reinterpret_cast<const std::uint8_t *>(
                        display.buffer.address),
                    memory_manager::kFrameBytes)
            : 0U;
    DrawBoxes(display, boxes);

    status = memory_hardware_->PrepareForPeripheralRead(display.buffer);
    if (!status.Ok()) {
        (void)memory_->ReleaseDisplayBuffer(display);
        return status;
    }
    if (log_copy_crc) {
        status = memory_hardware_->PrepareForCpuRead(display.buffer);
        if (!status.Ok()) {
            (void)memory_->ReleaseDisplayBuffer(display);
            return status;
        }
        const std::uint32_t memory_crc = Crc32(
            reinterpret_cast<const std::uint8_t *>(display.buffer.address),
            memory_manager::kFrameBytes);
        status = memory_hardware_->PrepareForPeripheralRead(display.buffer);
        if (!status.Ok()) {
            (void)memory_->ReleaseDisplayBuffer(display);
            return status;
        }
        tm_printf(reinterpret_cast<const UB *>(
                      "lcd: copy crc bytes=%u source=%x copied=%x psram=%x\n"),
                  static_cast<unsigned int>(memory_manager::kFrameBytes),
                  static_cast<unsigned int>(source_crc),
                  static_cast<unsigned int>(copied_crc),
                  static_cast<unsigned int>(memory_crc));
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
