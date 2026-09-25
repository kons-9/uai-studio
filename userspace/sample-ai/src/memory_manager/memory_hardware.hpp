#ifndef UAI_AI_MEMORY_HARDWARE_HPP
#define UAI_AI_MEMORY_HARDWARE_HPP

#include "common/error.hpp"
#include "driver/cache_controller/cache_controller.hpp"
#include "driver/nor_driver/nor_driver.hpp"
#include "driver/psram_driver/psram_driver.hpp"
#include "driver/rif_controller/rif_controller.hpp"
#include "memory_manager/memory_manager.hpp"

namespace uai::ai::memory_manager {

/* STM32-specific memory setup and cache-maintenance operations. Buffer
 * ownership and state transitions live in MemoryManager. */
class MemoryHardware final {
public:
    common::Error Initialize();
    /* PSRAM is always needed for camera buffers. Initialize NOR only when
     * model weights are needed; report its result through nor_status. */
    common::Error InitializeExternalMemory(int *nor_status,
                                          bool initialize_nor = true);
    common::Error InitializePeripheralAccess();

    common::Error PrepareForDmaWrite(const Buffer &buffer) const;
    common::Error PrepareForCpuRead(const Buffer &buffer) const;
    common::Error PrepareForPeripheralRead(const Buffer &buffer) const;

    void KeepInferenceClocksOnSleep() const;

private:
    driver::CacheController cache_{};
    driver::xspi::PsramDriver psram_{};
    driver::xspi::NorDriver nor_{};
    driver::RifController rif_{};
    bool initialized_ = false;
};

} // namespace uai::ai::memory_manager

#endif // UAI_AI_MEMORY_HARDWARE_HPP
