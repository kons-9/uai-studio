#ifndef UAI_AI_DRIVER_XSPI_NOR_HARDWARE_HPP
#define UAI_AI_DRIVER_XSPI_NOR_HARDWARE_HPP

#include <cstddef>
#include <cstdint>

namespace uai::ai::driver::xspi {

/* Board/BSP boundary for the model NOR.  No STM32 HAL or register type is
 * exposed to the application-facing NorDriver. */
class NorHardware final {
public:
    int Initialize();
    int Read(std::uint8_t *buffer, std::uint32_t address, std::size_t size);
    int EnableMemoryMappedMode();
};

} // namespace uai::ai::driver::xspi

#endif // UAI_AI_DRIVER_XSPI_NOR_HARDWARE_HPP
