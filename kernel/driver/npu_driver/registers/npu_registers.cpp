#include "driver/npu_driver/registers/npu_registers.hpp"

#include "ll_aton_platform.h"

namespace uai::ai::npu::registers {
namespace {

constexpr unsigned int kEpochController = 0U;
constexpr unsigned int kInterruptController = 0U;
constexpr unsigned int kBusInterface = 0U;
constexpr unsigned int kStreamEngine = 0U;

constexpr std::uint32_t kEpochInterruptMask = (1UL << ATON_EPOCHCTRL_INT(kEpochController))
    | (1UL << ATON_EPOCHCTRL_NOACK_INT(kEpochController)) | (1UL << ATON_EPOCHCTRL_ERR_INT(kEpochController));

} // namespace

NpuRegisterSnapshot NpuRegisterLayer::ReadSnapshot() const
{
    NpuRegisterSnapshot snapshot{};

    snapshot.epoch_control = ATON_EPOCHCTRL_CTRL_GET(kEpochController);
    snapshot.epoch_version = ATON_EPOCHCTRL_VERSION_GET(kEpochController);
    snapshot.epoch_address = ATON_EPOCHCTRL_ADDR_GET(kEpochController);
    snapshot.epoch_irq = ATON_EPOCHCTRL_IRQ_GET(kEpochController);
    snapshot.epoch_label = ATON_EPOCHCTRL_LABEL_GET(kEpochController);
    snapshot.epoch_byte_counter = ATON_EPOCHCTRL_BC_GET(kEpochController);

    snapshot.interrupt_control = ATON_INTCTRL_CTRL_GET(kInterruptController);
    snapshot.interrupt_status = ATON_INTCTRL_INTREG_GET(kInterruptController);
    snapshot.interrupt_set = ATON_INTCTRL_INTSET_GET(kInterruptController);
    snapshot.interrupt_clear = ATON_INTCTRL_INTCLR_GET(kInterruptController);
    snapshot.interrupt_or_mask = ATON_INTCTRL_INTORMSK_GET(kInterruptController, 0U);
    snapshot.interrupt_and_mask = ATON_INTCTRL_INTANDMSK_GET(kInterruptController, 0U);

    snapshot.busif0_control = ATON_BUSIF_CTRL_GET(kBusInterface);
    snapshot.busif0_error = ATON_BUSIF_ERR_GET(kBusInterface);
    snapshot.busif1_control = ATON_BUSIF_CTRL_GET(1U);
    snapshot.busif1_error = ATON_BUSIF_ERR_GET(1U);

    snapshot.stream0_control = ATON_STRENG_CTRL_GET(kStreamEngine);
    snapshot.stream0_address = ATON_STRENG_ADDR_GET(kStreamEngine);
    snapshot.stream0_frame_size = ATON_STRENG_FSIZE_GET(kStreamEngine);
    snapshot.stream0_depth = ATON_STRENG_DEPTH_GET(kStreamEngine);
    snapshot.stream0_limit_enable = ATON_STRENG_LIMITEN_GET(kStreamEngine);
    snapshot.stream0_limit = ATON_STRENG_LIMIT_GET(kStreamEngine);
    snapshot.stream0_limit_address = ATON_STRENG_LIMITADDR_GET(kStreamEngine);
    snapshot.stream0_depth_count = ATON_STRENG_DEPTHCNT_GET(kStreamEngine);
    snapshot.stream0_pixel_count = ATON_STRENG_PIXCNT_GET(kStreamEngine);
    snapshot.stream0_line_count = ATON_STRENG_LINECNT_GET(kStreamEngine);
    snapshot.stream0_frame_count = ATON_STRENG_FCNT_GET(kStreamEngine);
    snapshot.stream0_irq = ATON_STRENG_IRQ_GET(kStreamEngine);

    snapshot.epoch_enabled = ATON_EPOCHCTRL_CTRL_GET_EN(snapshot.epoch_control) != 0U;
    snapshot.epoch_running = ATON_EPOCHCTRL_CTRL_GET_RUNNING(snapshot.epoch_control) != 0U;
    snapshot.epoch_interrupt_pending = (snapshot.interrupt_status & kEpochInterruptMask) != 0U;
    snapshot.has_interrupt = snapshot.interrupt_status != 0U;
    snapshot.has_bus_error = snapshot.busif0_error != 0U;
    return snapshot;
}

} // namespace uai::ai::npu::registers
