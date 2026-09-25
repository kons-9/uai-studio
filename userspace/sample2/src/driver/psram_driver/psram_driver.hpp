#ifndef UAI_SAMPLE2_DRIVER_XSPI_PSRAM_DRIVER_HPP
#define UAI_SAMPLE2_DRIVER_XSPI_PSRAM_DRIVER_HPP

namespace uai::sample2::driver::xspi {

class PsramDriver final {
public:
    /* Initialize APS256XX PSRAM and expose it through the memory aperture. */
    bool Initialize();

private:
    bool initialized_ = false;
};

} // namespace uai::sample2::driver::xspi

#endif // UAI_SAMPLE2_DRIVER_XSPI_PSRAM_DRIVER_HPP
