#pragma once

#include <cstddef>
#include <cstdint>

#include "common/error.hpp"

namespace uai::ai::camera::sensor::registers {

/*
 * IMX335 register addresses used by the camera driver.
 *
 * Keep these values in this layer so sensor register knowledge does not leak
 * into the frame acquisition and display code.  The descriptions below are
 * deliberately short and match the names in the IMX335 component driver.
 */
enum class Imx335Register : std::uint16_t {
    kModeSelect = 0x3000, /* 0x00 streaming, 0x01 standby. */
    kFrameHold = 0x3001, /* Holds VMAX/shutter/gain updates atomically. */
    kVmax = 0x3030, /* Vertical frame length in lines (three bytes). */
    kShutter = 0x3058, /* Exposure/shutter position, three bytes. */
    kGain = 0x30E8, /* Analog gain code, two bytes. */
    kMipiClockSelect = 0x314C, /* MIPI clock profile, two bytes. */
    kMipiLaneClock = 0x315A, /* MIPI lane clock divider/profile. */
    kMipiSystemMode = 0x319E, /* MIPI system mode/profile. */
    kMipiTclkPost = 0x3A18, /* MIPI clock postamble, two bytes. */
    kMipiTclkPrepare = 0x3A1A, /* MIPI clock prepare time, two bytes. */
    kMipiTclkTrail = 0x3A1C, /* MIPI clock trail time, two bytes. */
    kMipiTclkZero = 0x3A1E, /* MIPI clock zero time, two bytes. */
    kMipiThsPrepare = 0x3A20, /* MIPI data prepare time, two bytes. */
    kMipiThsZero = 0x3A22, /* MIPI data zero time, two bytes. */
    kMipiThsTrail = 0x3A24, /* MIPI data trail time, two bytes. */
    kMipiThsExit = 0x3A26, /* MIPI data exit time, two bytes. */
    kMipiTplx = 0x3A28, /* MIPI LPX time, two bytes. */
    kTestPattern = 0x329E, /* Test pattern number (0..11). */
    kChipId = 0x3912, /* IMX335 chip identification register. */
};

struct RegisterDescription {
    Imx335Register address;
    const char *name;
    const char *description;
    std::uint8_t size;
};

struct SensorRegisterSnapshot {
    std::uint32_t vmax = 0U;
    std::uint32_t shutter = 0U;
    std::uint32_t gain = 0U;
};

/*
 * Register layer for the IMX335 I2C/SPI component bus.
 *
 * The actual bus remains owned by the board BSP.  Ai accesses it through
 * the IMX335 component object's bus callbacks; HAL and the opaque component
 * object stay out of this public header.
 */
class Imx335RegisterLayer final {
public:
    static const RegisterDescription *Describe(std::size_t *count);

    common::Error Read(Imx335Register address, void *data,
                       std::size_t size) const;
    common::Error Write(Imx335Register address, const void *data,
                        std::size_t size) const;

    common::Error SetStreaming(bool enabled) const;
    common::Error Configure(int test_pattern_mode, int32_t framerate) const;
    common::Error SetExposureMicroseconds(int32_t exposure) const;
    common::Error SetGainMilliDb(int32_t gain_mdB) const;
    /* Apply the known-good two-lane 891 Mbps timing profile. */
    common::Error ConfigureMipi891Mbps() const;
    common::Error ReadSnapshot(SensorRegisterSnapshot *snapshot) const;
};

} // namespace uai::ai::camera::sensor::registers
