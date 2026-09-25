#ifndef UAI_AI_DRIVER_XSPI_NOR_DRIVER_HPP
#define UAI_AI_DRIVER_XSPI_NOR_DRIVER_HPP

namespace uai::ai::driver::xspi {

/* Application-facing C++ driver for the STM32N6570-DK model NOR.
 * The board-specific HAL work remains in the STM32 BSP C implementation. */
class NorDriver final {
public:
    /* Initialize the NOR, verify the model-data aperture, and enable mapping.
     * Call after PSRAM initialization because BSP_XSPI_RAM_Init resets XSPIM. */
    int Initialize();

private:
    bool initialized_ = false;
};

} // namespace uai::ai::driver::xspi

#endif // UAI_AI_DRIVER_XSPI_NOR_DRIVER_HPP
