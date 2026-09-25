#include "driver/camera/registers/imx335_registers.hpp"

#include <cstring>

extern "C" {
#include "imx335.h"
extern void *Camera_CompObj;
}

namespace uai::ai::camera::registers {
namespace {

using common::Error;
using common::ErrorCode;

constexpr RegisterDescription kDescriptions[] = {
    {Imx335Register::kModeSelect, "MODE_SELECT",
     "Sensor streaming state: 0x00 stream, 0x01 standby.", 1U},
    {Imx335Register::kFrameHold, "HOLD",
     "Groups frame timing and exposure updates at a frame boundary.", 1U},
    {Imx335Register::kVmax, "VMAX",
     "Vertical frame length in sensor lines; controls frame period.", 3U},
    {Imx335Register::kShutter, "SHUTTER",
     "Exposure position in lines; lower value gives longer exposure.", 3U},
    {Imx335Register::kGain, "GAIN",
     "Analog gain code used by the IMX335 gain helper.", 2U},
    {Imx335Register::kMipiClockSelect, "INCKSEL1",
     "MIPI clock profile selected for the sensor link rate.", 2U},
    {Imx335Register::kMipiLaneClock, "INCKSEL2",
     "Second part of the MIPI lane clock profile.", 1U},
    {Imx335Register::kMipiSystemMode, "SYSMODE",
     "MIPI system mode paired with the selected lane clock.", 1U},
    {Imx335Register::kMipiTclkPost, "TCLKPOST",
     "MIPI clock postamble timing for the selected lane rate.", 2U},
    {Imx335Register::kMipiTclkPrepare, "TCLKPREPARE",
     "MIPI clock prepare timing for the selected lane rate.", 2U},
    {Imx335Register::kMipiTclkTrail, "TCLKTRAIL",
     "MIPI clock trail timing for the selected lane rate.", 2U},
    {Imx335Register::kMipiTclkZero, "TCLKZERO",
     "MIPI clock zero timing for the selected lane rate.", 2U},
    {Imx335Register::kMipiThsPrepare, "THSPREPARE",
     "MIPI data prepare timing for the selected lane rate.", 2U},
    {Imx335Register::kMipiThsZero, "THSZERO",
     "MIPI data zero timing for the selected lane rate.", 2U},
    {Imx335Register::kMipiThsTrail, "THSTRAIL",
     "MIPI data trail timing for the selected lane rate.", 2U},
    {Imx335Register::kMipiThsExit, "THSEXIT",
     "MIPI data exit timing for the selected lane rate.", 2U},
    {Imx335Register::kMipiTplx, "TPLX",
     "MIPI low-power transition timing for the selected lane rate.", 2U},
    {Imx335Register::kTestPattern, "TPG",
     "Sensor test-pattern selector; -1 is represented by the enable table.",
     1U},
    {Imx335Register::kChipId, "CHIP_ID",
     "Read-only sensor identification value.", 1U},
};

const RegisterDescription *Find(Imx335Register address)
{
    for (const RegisterDescription &description : kDescriptions) {
        if (description.address == address) {
            return &description;
        }
    }
    return nullptr;
}

Error HardwareError(const char *operation, int32_t status)
{
    return {ErrorCode::kHardware, static_cast<std::uint32_t>(status),
            operation};
}

} // namespace

const RegisterDescription *Imx335RegisterLayer::Describe(std::size_t *count)
{
    if (count != nullptr) {
        *count = sizeof(kDescriptions) / sizeof(kDescriptions[0]);
    }
    return kDescriptions;
}

Error Imx335RegisterLayer::Read(Imx335Register address, void *data,
                                std::size_t size) const
{
    const RegisterDescription *description = Find(address);
    if (description == nullptr || data == nullptr ||
        size != description->size || size > UINT16_MAX) {
        return {ErrorCode::kInvalidArgument, 0U, "camera.register.read"};
    }
    auto *sensor = static_cast<IMX335_Object_t *>(Camera_CompObj);
    if (sensor == nullptr) {
        return HardwareError("camera.register.read", -1);
    }
    const int32_t status = sensor->IO.ReadReg(
        sensor->IO.Address, static_cast<std::uint16_t>(address),
        static_cast<std::uint8_t *>(data), static_cast<std::uint16_t>(size));
    return status == 0 ? Error{ErrorCode::kOk, 0U, "camera.register.read"}
                       : HardwareError("camera.register.read", status);
}

Error Imx335RegisterLayer::Write(Imx335Register address, const void *data,
                                 std::size_t size) const
{
    const RegisterDescription *description = Find(address);
    if (description == nullptr || data == nullptr ||
        size != description->size || size > UINT16_MAX) {
        return {ErrorCode::kInvalidArgument, 0U, "camera.register.write"};
    }
    auto *sensor = static_cast<IMX335_Object_t *>(Camera_CompObj);
    if (sensor == nullptr) {
        return HardwareError("camera.register.write", -1);
    }
    const int32_t status = sensor->IO.WriteReg(
        sensor->IO.Address, static_cast<std::uint16_t>(address),
        const_cast<std::uint8_t *>(static_cast<const std::uint8_t *>(data)),
        static_cast<std::uint16_t>(size));
    return status == 0 ? Error{ErrorCode::kOk, 0U, "camera.register.write"}
                       : HardwareError("camera.register.write", status);
}

Error Imx335RegisterLayer::SetStreaming(bool enabled) const
{
    const std::uint8_t mode = enabled ? 0x00U : 0x01U;
    return Write(Imx335Register::kModeSelect, &mode, sizeof(mode));
}

Error Imx335RegisterLayer::ConfigureMipi891Mbps() const
{
    const std::uint8_t incksel1[] = {0x29U, 0x01U};
    const std::uint8_t incksel2 = 0x06U;
    const std::uint8_t sysmode = 0x02U;
    struct Timing {
        Imx335Register address;
        std::uint8_t low;
        std::uint8_t high;
    };
    constexpr Timing timings[] = {
        {Imx335Register::kMipiTclkPost, 0x7FU, 0x00U},
        {Imx335Register::kMipiTclkPrepare, 0x37U, 0x00U},
        {Imx335Register::kMipiTclkTrail, 0x37U, 0x00U},
        {Imx335Register::kMipiTclkZero, 0xF7U, 0x00U},
        {Imx335Register::kMipiThsPrepare, 0x3FU, 0x00U},
        {Imx335Register::kMipiThsZero, 0x6FU, 0x00U},
        {Imx335Register::kMipiThsTrail, 0x3FU, 0x00U},
        {Imx335Register::kMipiThsExit, 0x5FU, 0x00U},
        {Imx335Register::kMipiTplx, 0x2FU, 0x00U},
    };
    Error status = Write(Imx335Register::kMipiClockSelect,
                          incksel1, sizeof(incksel1));
    if (!status.Ok()) return status;
    status = Write(Imx335Register::kMipiLaneClock, &incksel2, sizeof(incksel2));
    if (!status.Ok()) return status;
    status = Write(Imx335Register::kMipiSystemMode, &sysmode, sizeof(sysmode));
    if (!status.Ok()) return status;
    for (const Timing &timing : timings) {
        const std::uint8_t value[] = {timing.low, timing.high};
        status = Write(timing.address, value, sizeof(value));
        if (!status.Ok()) return status;
    }
    return {ErrorCode::kOk, 0U, "camera.register.mipi_891"};
}

Error Imx335RegisterLayer::ReadSnapshot(SensorRegisterSnapshot *snapshot) const
{
    if (snapshot == nullptr) {
        return {ErrorCode::kInvalidArgument, 0U,
                "camera.register.snapshot"};
    }
    std::uint8_t vmax[3] = {};
    std::uint8_t shutter[3] = {};
    std::uint8_t gain[2] = {};
    Error status = Read(Imx335Register::kVmax, vmax, sizeof(vmax));
    if (!status.Ok()) {
        return status;
    }
    status = Read(Imx335Register::kShutter, shutter, sizeof(shutter));
    if (!status.Ok()) {
        return status;
    }
    status = Read(Imx335Register::kGain, gain, sizeof(gain));
    if (!status.Ok()) {
        return status;
    }
    snapshot->vmax = static_cast<std::uint32_t>(vmax[0]) |
                     (static_cast<std::uint32_t>(vmax[1]) << 8U) |
                     (static_cast<std::uint32_t>(vmax[2]) << 16U);
    snapshot->shutter = static_cast<std::uint32_t>(shutter[0]) |
                        (static_cast<std::uint32_t>(shutter[1]) << 8U) |
                        (static_cast<std::uint32_t>(shutter[2]) << 16U);
    snapshot->gain = static_cast<std::uint32_t>(gain[0]) |
                     (static_cast<std::uint32_t>(gain[1]) << 8U);
    return {ErrorCode::kOk, 0U, "camera.register.snapshot"};
}

} // namespace uai::ai::camera::registers
