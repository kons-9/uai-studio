#ifndef UAI_SAMPLE2_LCD_DRIVER_HPP
#define UAI_SAMPLE2_LCD_DRIVER_HPP

#include "common/error.hpp"
#include "memory_manager/memory_hardware.hpp"
#include "memory_manager/memory_manager.hpp"

#include "driver/display_driver.hpp"

namespace uai::sample2 {

class LcdDriver final {
public:
    common::Error Initialize(memory_manager::MemoryManager &memory,
                             memory_manager::MemoryHardware &memory_hardware);
    common::Error ShowInitialFrame(
        const memory_manager::BoxSet &boxes,
        bool coordinate_pattern = false);
    common::Error SynchronizeCurrentFrame();
    common::Error GenerateCoordinatePattern(
        const memory_manager::Buffer &destination) const;
    common::Error ComposeAndPresent(
        const memory_manager::CaptureFrame &capture,
        const memory_manager::BoxSet &boxes,
        bool log_copy_crc = false);

private:
    static void FillInitialFrame(const memory_manager::DisplayBuffer &buffer,
                                 const memory_manager::BoxSet &boxes,
                                 bool coordinate_pattern);
    static void DrawBoxes(const memory_manager::DisplayBuffer &buffer,
                          const memory_manager::BoxSet &boxes);
    static common::Error FromBackend(uai::driver::DriverStatus status,
                                     const char *operation);

    memory_manager::MemoryManager *memory_ = nullptr;
    memory_manager::MemoryHardware *memory_hardware_ = nullptr;
    uai::driver::DisplayDriver backend_{};
    bool initialized_ = false;
};

} // namespace uai::sample2

#endif
