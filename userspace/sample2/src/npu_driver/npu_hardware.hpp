#ifndef UAI_SAMPLE2_NPU_HARDWARE_HPP
#define UAI_SAMPLE2_NPU_HARDWARE_HPP

#include <cstdint>

namespace uai::sample2::npu_driver {

/*
 * A read-only view of the Neural-ART/NPU hardware state.  The raw register
 * values are intentionally kept here: a timeout must remain diagnosable even
 * when a future NPU revision changes the meaning of one of the fields.
 */
struct HardwareSnapshot {
    std::uint32_t epoch_control = 0U;
    std::uint32_t epoch_version = 0U;
    std::uint32_t epoch_address = 0U;
    std::uint32_t epoch_irq = 0U;
    std::uint32_t epoch_label = 0U;
    std::uint32_t epoch_byte_counter = 0U;

    std::uint32_t interrupt_control = 0U;
    std::uint32_t interrupt_status = 0U;
    std::uint32_t interrupt_set = 0U;
    std::uint32_t interrupt_clear = 0U;
    std::uint32_t interrupt_or_mask = 0U;
    std::uint32_t interrupt_and_mask = 0U;

    std::uint32_t busif0_control = 0U;
    std::uint32_t busif0_error = 0U;

    std::uint32_t stream0_control = 0U;
    std::uint32_t stream0_address = 0U;
    std::uint32_t stream0_frame_size = 0U;
    std::uint32_t stream0_depth = 0U;
    std::uint32_t stream0_limit_enable = 0U;
    std::uint32_t stream0_limit = 0U;
    std::uint32_t stream0_limit_address = 0U;
    std::uint32_t stream0_depth_count = 0U;
    std::uint32_t stream0_pixel_count = 0U;
    std::uint32_t stream0_line_count = 0U;
    std::uint32_t stream0_frame_count = 0U;
    std::uint32_t stream0_irq = 0U;

    bool epoch_enabled = false;
    bool epoch_running = false;
    bool epoch_interrupt_pending = false;
    bool has_interrupt = false;
    bool has_bus_error = false;
};

class NpuHardware final {
public:
    HardwareSnapshot ReadSnapshot() const;
};

} // namespace uai::sample2::npu_driver

#endif
