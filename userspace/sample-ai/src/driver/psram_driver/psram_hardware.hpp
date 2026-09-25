#ifndef UAI_AI_DRIVER_XSPI_PSRAM_HARDWARE_HPP
#define UAI_AI_DRIVER_XSPI_PSRAM_HARDWARE_HPP

namespace uai::ai::driver::xspi {

/* Board/BSP boundary for the external PSRAM. */
class PsramHardware final {
public:
    bool Initialize();
};

} // namespace uai::ai::driver::xspi

#endif // UAI_AI_DRIVER_XSPI_PSRAM_HARDWARE_HPP
