#ifndef UAI_AI_DRIVER_XSPI_PSRAM_DRIVER_HPP
#define UAI_AI_DRIVER_XSPI_PSRAM_DRIVER_HPP

namespace uai::ai::driver::xspi {

class PsramDriver final {
public:
    /* Initialize APS256XX PSRAM and expose it through the memory aperture. */
    bool Initialize();

private:
    bool initialized_ = false;
};

} // namespace uai::ai::driver::xspi

#endif // UAI_AI_DRIVER_XSPI_PSRAM_DRIVER_HPP
