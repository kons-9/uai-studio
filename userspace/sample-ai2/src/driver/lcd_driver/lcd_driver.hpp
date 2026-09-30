#ifndef UAI_AI_LCD_DRIVER_HPP
#define UAI_AI_LCD_DRIVER_HPP

#include <cstdint>

#include "common/error.hpp"
#include "driver/driver_status.hpp"
#include "driver/driver_ownership.hpp"
#include "driver/lcd_driver/registers/lcd_registers.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "application/pipeline/frame_types.hpp"
#include "memory_manager/memory_manager.hpp"
#include "models/inference_result_types.hpp"

namespace uai::ai::lcd {

class LcdDriver final {
public:
    using Writer = driver::ResourceManagement::Writer;

    common::Error Initialize(memory_manager::MemoryManager &memory,
                             cache::CacheDriver &cache);
    common::Error AcquireWriter(Writer *writer, TMO timeout = TMO_FEVR) const
    { return management_.Acquire(writer, timeout); }
    void KeepClocksOnSleep() const;
    void KeepClocksOnSleep(const Writer &writer) const;
    common::Error ShowInitialFrame(
        const inference::BoxSet &boxes,
        bool coordinate_pattern = false);
    common::Error ShowInitialFrame(
        const inference::BoxSet &boxes, const Writer &writer,
        bool coordinate_pattern = false);
    common::Error SynchronizeCurrentFrame();
    common::Error SynchronizeCurrentFrame(const Writer &writer);
    common::Error GenerateCoordinatePattern(
        const memory_allocator::Buffer &destination) const;
    common::Error GenerateCoordinatePattern(
        const memory_allocator::Buffer &destination,
        const Writer &writer) const;
    common::Error ComposeAndPresent(
        const pipeline::CaptureFrame &capture,
        const inference::BoxSet &boxes,
        bool log_copy_crc = false);
    common::Error ComposeAndPresent(
        const pipeline::CaptureFrame &capture,
        const inference::BoxSet &boxes, const Writer &writer,
        bool log_copy_crc = false);
    common::Error ComposeInferenceAndPresent(
        const pipeline::InferenceFrame &frame);
    common::Error ComposeInferenceAndPresent(
        const pipeline::InferenceFrame &frame, const Writer &writer);
    void SetTimingDiagnostics(bool enabled) { timing_diagnostics_ = enabled; }

private:
    driver::ResourceManagement management_{};
    static void FillInitialFrame(const pipeline::DisplayBuffer &buffer,
                                 const inference::BoxSet &boxes,
                                 bool coordinate_pattern);
    static void DrawBoxes(const pipeline::DisplayBuffer &buffer,
                          const inference::BoxSet &boxes);
    static void DrawInferenceRegion(
        const pipeline::DisplayBuffer &buffer);
    static void DrawMask(const pipeline::DisplayBuffer &buffer,
                         const inference::BoxSet &boxes);
    static common::Error FromBackend(uai::driver::DriverStatus status,
                                     const char *operation);
    memory_manager::MemoryManager *memory_ = nullptr;
    cache::CacheDriver *cache_ = nullptr;
    registers::LcdRegisterLayer registers_{};
    bool initialized_ = false;
    bool timing_diagnostics_ = false;
};

} // namespace uai::ai::lcd

#endif
