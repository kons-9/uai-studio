#ifndef UAI_AI_LCD_DRIVER_HPP
#define UAI_AI_LCD_DRIVER_HPP

#include <cstdint>

#include "common/error.hpp"
#include "driver/driver_status.hpp"
#include "driver/lcd_driver/registers/lcd_registers.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "memory_allocator/memory_allocator.hpp"

namespace uai::ai::lcd {

class LcdDriver final {
public:
    common::Error Initialize(memory_allocator::MemoryAllocator &memory,
                             cache::CacheDriver &cache);
    void KeepClocksOnSleep() const;
    common::Error ShowInitialFrame(
        const memory_allocator::BoxSet &boxes,
        bool coordinate_pattern = false);
    common::Error SynchronizeCurrentFrame();
    common::Error GenerateCoordinatePattern(
        const memory_allocator::Buffer &destination) const;
    common::Error ComposeAndPresent(
        const memory_allocator::CaptureFrame &capture,
        const memory_allocator::BoxSet &boxes,
        bool log_copy_crc = false);
    common::Error ComposeInferenceAndPresent(
        const memory_allocator::InferenceFrame &frame);
    void SetTimingDiagnostics(bool enabled) { timing_diagnostics_ = enabled; }

private:
    static void FillInitialFrame(const memory_allocator::DisplayBuffer &buffer,
                                 const memory_allocator::BoxSet &boxes,
                                 bool coordinate_pattern);
    static void DrawBoxes(const memory_allocator::DisplayBuffer &buffer,
                          const memory_allocator::BoxSet &boxes);
    static void DrawInferenceRegion(
        const memory_allocator::DisplayBuffer &buffer);
    static void DrawMask(const memory_allocator::DisplayBuffer &buffer,
                         const memory_allocator::BoxSet &boxes);
    static common::Error FromBackend(uai::driver::DriverStatus status,
                                     const char *operation);
    memory_allocator::MemoryAllocator *memory_ = nullptr;
    cache::CacheDriver *cache_ = nullptr;
    registers::LcdRegisterLayer registers_{};
    bool initialized_ = false;
    bool timing_diagnostics_ = false;
};

} // namespace uai::ai::lcd

#endif
