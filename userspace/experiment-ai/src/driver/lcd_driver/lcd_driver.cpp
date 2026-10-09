#include "driver/lcd_driver/lcd_driver.hpp"
#include "common/log.hpp"

#include <cstddef>
#include <cstring>

extern "C" {
#include <tm/tmonitor.h>
#include "stm32n6xx_hal.h"
}

namespace uai::ai::lcd {

namespace {

constexpr std::uint16_t kRed = 0xF800U;
constexpr std::uint16_t kFaceBlue = 0x001FU;
constexpr std::uint16_t kSegmentationGreen = 0x07E0U;
constexpr std::uint16_t kInferenceRegionColor = kSegmentationGreen;
constexpr std::int32_t kLineWidth = 4;
constexpr std::size_t kInferenceDisplaySize = memory_allocator::kConfig.inference_width;
constexpr std::size_t kInferenceDisplayX = (memory_allocator::kConfig.frame_width - kInferenceDisplaySize) / 2U;
constexpr std::uint16_t kInitialPattern[] = {
    0xFFFFU,
    0xFFE0U,
    0x07FFU,
    0x07E0U,
    0xF81FU,
    0xF800U,
    0x001FU,
    0x0000U,
};

bool InRange(
    std::int32_t value,
    std::int32_t limit
)
{
    return value >= 0 && value < limit;
}

std::uint16_t CoordinatePatternPixel(
    std::size_t x,
    std::size_t y
)
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

std::uint32_t Crc32(
    const std::uint8_t *bytes,
    std::size_t size
)
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

common::Error LcdDriver::FromBackend(
    uai::driver::DriverStatus status,
    const char *operation
)
{
    if (uai::driver::IsOk(status)) {
        return {common::ErrorCode::kOk, 0U, operation};
    }
    if (status == uai::driver::DriverStatus::kBusy) {
        return {common::ErrorCode::kNoBuffer, 0U, operation};
    }
    return {common::ErrorCode::kHardware, static_cast<std::uint32_t>(status), operation};
}

common::Error LcdDriver::Initialize(
    memory_allocator::MemoryAllocator &memory,
    cache::CacheDriver &cache
)
{
    if (initialized_) {
        return {common::ErrorCode::kAlreadyInitialized, 0U, "lcd.initialize"};
    }
    memory_ = &memory;
    cache_ = &cache;
    const uai::driver::DriverStatus status = registers_.Initialize();
    if (!uai::driver::IsOk(status)) {
        memory_ = nullptr;
        return FromBackend(status, "lcd.initialize");
    }
    initialized_ = true;
    return {common::ErrorCode::kOk, 0U, "lcd.initialize"};
}

void LcdDriver::KeepClocksOnSleep() const
{
    __HAL_RCC_LTDC_CLK_SLEEP_ENABLE();
    __HAL_RCC_DMA2D_CLK_SLEEP_ENABLE();
}

void LcdDriver::FillInitialFrame(
    const memory_allocator::DisplayBuffer &buffer,
    const memory_allocator::BoxSet &boxes,
    bool coordinate_pattern
)
{
    auto *pixels = reinterpret_cast<std::uint16_t *>(buffer.buffer.address);
    for (std::size_t y = 0U; y < memory_allocator::kConfig.frame_height; ++y) {
        for (std::size_t x = 0U; x < memory_allocator::kConfig.frame_width; ++x) {
            if (coordinate_pattern) {
                pixels[y * memory_allocator::kConfig.frame_width + x] = CoordinatePatternPixel(x, y);
                continue;
            }
            const std::size_t color =
                x * (sizeof(kInitialPattern) / sizeof(kInitialPattern[0])) / memory_allocator::kConfig.frame_width;
            pixels[y * memory_allocator::kConfig.frame_width + x] = kInitialPattern[color];
        }
    }
    DrawBoxes(buffer, boxes);
}

common::Error LcdDriver::GenerateCoordinatePattern(const memory_allocator::Buffer &destination) const
{
    if (!initialized_ || destination.region != memory_allocator::Region::kCapture
        || destination.size != memory_allocator::kConfig.frame_bytes() || destination.address == 0U) {
        return {common::ErrorCode::kInvalidArgument, 0U, "lcd.generate_coordinate_pattern"};
    }
    auto *pixels = reinterpret_cast<std::uint16_t *>(destination.address);
    for (std::size_t y = 0U; y < memory_allocator::kConfig.frame_height; ++y) {
        for (std::size_t x = 0U; x < memory_allocator::kConfig.frame_width; ++x) {
            pixels[y * memory_allocator::kConfig.frame_width + x] = CoordinatePatternPixel(x, y);
        }
    }
    return {common::ErrorCode::kOk, 0U, "lcd.generate_coordinate_pattern"};
}

void LcdDriver::DrawBoxes(
    const memory_allocator::DisplayBuffer &buffer,
    const memory_allocator::BoxSet &boxes
)
{
    auto *pixels = reinterpret_cast<std::uint16_t *>(buffer.buffer.address);
    const std::int32_t width = static_cast<std::int32_t>(memory_allocator::kConfig.frame_width);
    const std::int32_t height = static_cast<std::int32_t>(memory_allocator::kConfig.frame_height);
    const auto draw_set = [&](const memory_allocator::DetectionSet &set, std::uint16_t color) {
        const std::uint32_t count =
            set.count < memory_allocator::kConfig.max_boxes ? set.count : memory_allocator::kConfig.max_boxes;
        for (std::uint32_t i = 0U; i < count; ++i) {
            const memory_allocator::Box &box = set.boxes[i];
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
                        pixels[y_top * width + x] = color;
                    }
                    if (InRange(x, width) && InRange(y_bottom, height)) {
                        pixels[y_bottom * width + x] = color;
                    }
                }
                for (std::int32_t y = top; y <= bottom; ++y) {
                    if (InRange(x_left, width) && InRange(y, height)) {
                        pixels[y * width + x_left] = color;
                    }
                    if (InRange(x_right, width) && InRange(y, height)) {
                        pixels[y * width + x_right] = color;
                    }
                }
            }
        }
    };

    /* Separate receivers keep the visual result unambiguous:
     * person=red, face=blue, segmentation=green mask. */
    draw_set(boxes.person, kRed);
    draw_set(boxes.face, kFaceBlue);
}

void LcdDriver::DrawInferenceRegion(const memory_allocator::DisplayBuffer &buffer)
{
    (void)buffer;
}

void LcdDriver::DrawMask(
    const memory_allocator::DisplayBuffer &buffer,
    const memory_allocator::BoxSet &boxes
)
{
    if (boxes.segmentation.mask_address == 0U || boxes.segmentation.mask_width == 0U
        || boxes.segmentation.mask_height == 0U) {
        return;
    }

    const auto *mask = reinterpret_cast<const std::uint8_t *>(boxes.segmentation.mask_address);
    auto *pixels = reinterpret_cast<std::uint16_t *>(buffer.buffer.address);
    /* A mask is emitted only by the segmentation postprocessor. Pipe2 keeps
     * the complete Pipe1 crop and letterboxes it into the square model input.
     * Scale the live 16:9 content and its padding into the model mask, which
     * may be lower resolution than the input (20x20 for segmentation). */
    constexpr std::size_t crop_width = memory_allocator::kConfig.frame_width;
    constexpr std::size_t crop_x = 0U;
    constexpr std::size_t kModelInputHeight = 320U;
    constexpr std::size_t kModelContentHeight = 192U;
    const std::size_t mask_content_height = boxes.segmentation.mask_height * kModelContentHeight / kModelInputHeight;
    const std::size_t mask_pad_top = (boxes.segmentation.mask_height - mask_content_height) / 2U;
    for (std::size_t y = 0U; y < memory_allocator::kConfig.frame_height; ++y) {
        const std::size_t mask_y = mask_pad_top + y * mask_content_height / memory_allocator::kConfig.frame_height;
        for (std::size_t x = 0U; x < crop_width; ++x) {
            const std::size_t mask_x = x * boxes.segmentation.mask_width / crop_width;
            if (mask[mask_y * boxes.segmentation.mask_width + mask_x] == 0U) {
                continue;
            }
            const std::size_t pixel_index = y * memory_allocator::kConfig.frame_width + crop_x + x;
            const std::uint16_t original = pixels[pixel_index];
            const std::uint16_t red = static_cast<std::uint16_t>((original >> 11U) & 0x1FU);
            const std::uint16_t green = static_cast<std::uint16_t>((original >> 5U) & 0x3FU);
            const std::uint16_t blue = static_cast<std::uint16_t>(original & 0x1FU);
            pixels[pixel_index] =
                static_cast<std::uint16_t>(((red / 2U) << 11U) | (((green + 63U) / 2U) << 5U) | (blue / 2U));
        }
    }
}

common::Error LcdDriver::ShowInitialFrame(
    const memory_allocator::BoxSet &boxes,
    bool coordinate_pattern
)
{
    if (!initialized_ || memory_ == nullptr) {
        return {common::ErrorCode::kNotInitialized, 0U, "lcd.show_initial"};
    }

    memory_allocator::DisplayBuffer first{};
    common::Error status = memory_->AcquireDisplayBuffer(&first);
    if (!status.Ok()) {
        return status;
    }
    FillInitialFrame(first, boxes, coordinate_pattern);
    status = cache_->PrepareForPeripheralRead(first.buffer);
    if (!status.Ok()) {
        (void)memory_->ReleaseDisplayBuffer(first);
        return status;
    }
    const uai::driver::DriverStatus backend_status = registers_.Present(first.buffer.address);
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
    memory_allocator::DisplayBuffer spare{};
    status = memory_->AcquireDisplayBuffer(&spare);
    if (status.Ok()) {
        FillInitialFrame(spare, boxes, coordinate_pattern);
        status = cache_->PrepareForPeripheralRead(spare.buffer);
        if (status.Ok()) {
            status = memory_->ReleaseDisplayBuffer(spare);
        } else {
            (void)memory_->ReleaseDisplayBuffer(spare);
        }
    }
    return status.Ok() ? common::Error{common::ErrorCode::kOk, 0U, "lcd.show_initial"} : status;
}

common::Error LcdDriver::SynchronizeCurrentFrame()
{
    if (!initialized_ || memory_ == nullptr) {
        return {common::ErrorCode::kNotInitialized, 0U, "lcd.synchronize_current_frame"};
    }
    const uai::driver::DriverStatus status = registers_.Synchronize();
    if (!uai::driver::IsOk(status)) {
        return FromBackend(status, "lcd.synchronize_current_frame");
    }
    return memory_->CompleteDisplayHandoff();
}

common::Error LcdDriver::ComposeAndPresent(
    const memory_allocator::CaptureFrame &capture,
    const memory_allocator::BoxSet &boxes,
    bool log_copy_crc
)
{
    if (!initialized_ || memory_ == nullptr) {
        return {common::ErrorCode::kNotInitialized, 0U, "lcd.compose"};
    }
    if (!capture) {
        return {common::ErrorCode::kInvalidArgument, 0U, "lcd.compose"};
    }
    common::Error status{};

    /* Do not reacquire the previous active surface until LTDC confirms that
     * the queued VBlank reload has latched the new CFBAR. */
    const uai::driver::DriverStatus sync_status = registers_.Synchronize();
    if (sync_status == uai::driver::DriverStatus::kBusy) {
        return {common::ErrorCode::kNoBuffer, 0U, "lcd.reload.pending"};
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
    status = cache_->PrepareForCpuRead(capture.buffer);
    if (!status.Ok()) {
        return status;
    }
    const auto *source_bytes = reinterpret_cast<const std::uint8_t *>(capture.buffer.address);
    const std::uint32_t source_crc = log_copy_crc ? Crc32(source_bytes, memory_allocator::kConfig.frame_bytes()) : 0U;

    memory_allocator::DisplayBuffer display{};
    status = memory_->AcquireDisplayBuffer(&display);
    if (!status.Ok()) {
        return status;
    }
    std::uint32_t copy_start_tick = 0U;
    if (timing_diagnostics_) {
        copy_start_tick = HAL_GetTick();
    }
    std::memcpy(
        reinterpret_cast<void *>(display.buffer.address),
        reinterpret_cast<const void *>(capture.buffer.address),
        memory_allocator::kConfig.frame_bytes()
    );
    if (timing_diagnostics_) {
        const std::uint32_t copy_elapsed_ms = HAL_GetTick() - copy_start_tick;
        static std::uint32_t copy_window_start = 0U;
        static std::uint32_t copy_count = 0U;
        static std::uint32_t copy_total_ms = 0U;
        static std::uint32_t copy_max_ms = 0U;
        const std::uint32_t copy_now = HAL_GetTick();
        if (copy_window_start == 0U) {
            copy_window_start = copy_now;
        }
        ++copy_count;
        copy_total_ms += copy_elapsed_ms;
        if (copy_elapsed_ms > copy_max_ms) {
            copy_max_ms = copy_elapsed_ms;
        }
        if (copy_now - copy_window_start >= 1000U) {
            UB line[128] = {};
            (void)tm_sprintf(
                line,
                reinterpret_cast<const UB *>("lcd: cpu_copy count=%u total_ms=%u max_ms=%u "
                                             "window_ms=%u\n"),
                static_cast<unsigned int>(copy_count),
                static_cast<unsigned int>(copy_total_ms),
                static_cast<unsigned int>(copy_max_ms),
                static_cast<unsigned int>(copy_now - copy_window_start)
            );
            UAI_LOG_TEXT(uai::ai::common::LogLevel::kDebug, line);
            copy_window_start = copy_now;
            copy_count = 0U;
            copy_total_ms = 0U;
            copy_max_ms = 0U;
        }
    }
    const std::uint32_t copied_crc = log_copy_crc
        ? Crc32(reinterpret_cast<const std::uint8_t *>(display.buffer.address), memory_allocator::kConfig.frame_bytes())
        : 0U;
    DrawMask(display, boxes);
    DrawInferenceRegion(display);
    DrawBoxes(display, boxes);

    status = cache_->PrepareForPeripheralRead(display.buffer);
    if (!status.Ok()) {
        (void)memory_->ReleaseDisplayBuffer(display);
        return status;
    }
    if (log_copy_crc) {
        status = cache_->PrepareForCpuRead(display.buffer);
        if (!status.Ok()) {
            (void)memory_->ReleaseDisplayBuffer(display);
            return status;
        }
        const std::uint32_t memory_crc = Crc32(
            reinterpret_cast<const std::uint8_t *>(display.buffer.address), memory_allocator::kConfig.frame_bytes()
        );
        status = cache_->PrepareForPeripheralRead(display.buffer);
        if (!status.Ok()) {
            (void)memory_->ReleaseDisplayBuffer(display);
            return status;
        }
        UAI_LOG_DEBUG(
            reinterpret_cast<const UB *>("lcd: copy crc bytes=%u source=%x copied=%x psram=%x\n"),
            static_cast<unsigned int>(memory_allocator::kConfig.frame_bytes()),
            static_cast<unsigned int>(source_crc),
            static_cast<unsigned int>(copied_crc),
            static_cast<unsigned int>(memory_crc)
        );
    }
    const uai::driver::DriverStatus backend_status = registers_.Present(display.buffer.address);
    if (!uai::driver::IsOk(backend_status)) {
        (void)memory_->ReleaseDisplayBuffer(display);
        return FromBackend(backend_status, "lcd.present");
    }
    return memory_->CommitDisplayBuffer(display);
}

common::Error LcdDriver::ComposeInferenceAndPresent(const memory_allocator::InferenceFrame &frame)
{
    if (!initialized_ || memory_ == nullptr || cache_ == nullptr) {
        return {common::ErrorCode::kNotInitialized, 0U, "lcd.compose_inference"};
    }
    if (!frame || !frame.from_pipe2 || frame.buffer.size < memory_allocator::kConfig.inference_frame_bytes()) {
        return {common::ErrorCode::kInvalidArgument, 0U, "lcd.compose_inference"};
    }

    /* Ensure the previous reload has latched before reusing the other LCD
     * surface. This is the same handoff discipline as ComposeAndPresent(). */
    const uai::driver::DriverStatus sync_status = registers_.Synchronize();
    if (sync_status == uai::driver::DriverStatus::kBusy) {
        return {common::ErrorCode::kNoBuffer, 0U, "lcd.inference_reload.pending"};
    }
    if (!uai::driver::IsOk(sync_status)) {
        return FromBackend(sync_status, "lcd.inference_reload.wait");
    }
    common::Error status = memory_->CompleteDisplayHandoff();
    if (!status.Ok()) {
        return status;
    }

    const memory_allocator::Buffer input_buffer{
        frame.buffer.address,
        memory_allocator::kConfig.inference_frame_bytes(),
        frame.buffer.index,
        memory_allocator::Region::kInference
    };
    status = cache_->PrepareForCpuRead(input_buffer);
    if (!status.Ok()) {
        return status;
    }

    memory_allocator::DisplayBuffer display{};
    status = memory_->AcquireDisplayBuffer(&display);
    if (!status.Ok()) {
        return status;
    }

    const auto *source = reinterpret_cast<const std::uint8_t *>(frame.buffer.address);
    auto *destination = reinterpret_cast<std::uint16_t *>(display.buffer.address);
    for (std::size_t y = 0U; y < memory_allocator::kConfig.frame_height; ++y) {
        for (std::size_t x = 0U; x < memory_allocator::kConfig.frame_width; ++x) {
            destination[y * memory_allocator::kConfig.frame_width + x] = 0U;
        }
    }
    for (std::size_t y = 0U; y < kInferenceDisplaySize; ++y) {
        const std::size_t source_y = y * memory_allocator::kConfig.inference_height / kInferenceDisplaySize;
        for (std::size_t x = 0U; x < kInferenceDisplaySize; ++x) {
            const std::size_t source_x = x * memory_allocator::kConfig.inference_width / kInferenceDisplaySize;
            const std::size_t source_index = (source_y * memory_allocator::kConfig.inference_width + source_x) * 3U;
            const std::uint16_t red = static_cast<std::uint16_t>(source[source_index] >> 3U);
            const std::uint16_t green = static_cast<std::uint16_t>(source[source_index + 1U] >> 2U);
            const std::uint16_t blue = static_cast<std::uint16_t>(source[source_index + 2U] >> 3U);
            destination[y * memory_allocator::kConfig.frame_width + kInferenceDisplayX + x] =
                static_cast<std::uint16_t>((red << 11U) | (green << 5U) | blue);
        }
    }

    status = cache_->PrepareForPeripheralRead(display.buffer);
    if (!status.Ok()) {
        (void)memory_->ReleaseDisplayBuffer(display);
        return status;
    }
    const uai::driver::DriverStatus backend_status = registers_.Present(display.buffer.address);
    if (!uai::driver::IsOk(backend_status)) {
        (void)memory_->ReleaseDisplayBuffer(display);
        return FromBackend(backend_status, "lcd.present_inference");
    }
    return memory_->CommitDisplayBuffer(display);
}

} // namespace uai::ai::lcd
