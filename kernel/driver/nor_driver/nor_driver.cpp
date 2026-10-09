#include "driver/nor_driver/nor_driver.hpp"
#include "driver/config/ai_board_config.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
}

namespace uai::ai::nor {

int NorDriver::Initialize(const Writer &writer)
{
    if (!NorManagement::Instance().Validate(writer).Ok()) {
        return -1;
    }
    if (initialized_) {
        return 0;
    }

    const int init_status = registers_.Initialize();
    if (init_status != 0) {
        return -1;
    }
    read_ready_ = true;

    /* Probe the model weights before switching the NOR to memory-mapped mode. */
    uint8_t model_probe[16] = {};
    const int read_status = registers_.Read(model_probe, config::kModelNorProbeOffset, sizeof(model_probe));
    if (read_status != 0) {
        return -1;
    }

    initialized_ = registers_.EnableMemoryMappedMode() == 0;
    return initialized_ ? 0 : -1;
}

common::Error NorDriver::PrepareRead(const Writer &writer)
{
    const auto ownership = NorManagement::Instance().Validate(writer);
    if (!ownership.Ok())
        return ownership;
    if (read_ready_)
        return {};
    read_ready_ = registers_.Initialize() == 0;
    return {read_ready_ ? common::ErrorCode::kOk : common::ErrorCode::kHardware};
}

common::Error NorDriver::Read(
    std::uint32_t address,
    std::uint8_t *output,
    std::size_t bytes,
    const Writer &writer
)
{
    const auto ownership = NorManagement::Instance().Validate(writer);
    if (!ownership.Ok())
        return ownership;
    if (!read_ready_)
        return {common::ErrorCode::kNotInitialized};
    constexpr std::uint32_t capacity = 128U * 1024U * 1024U;
    if (output == nullptr || bytes == 0 || address >= capacity || bytes > capacity - address) {
        return {common::ErrorCode::kInvalidArgument};
    }
    return {registers_.Read(output, address, bytes) == 0 ? common::ErrorCode::kOk : common::ErrorCode::kHardware};
}

NorDriver::Diagnostic NorDriver::Diagnostics(const Writer &writer) const
{
    return NorManagement::Instance().Validate(writer).Ok() ? registers_.Diagnostics() : Diagnostic{};
}

void NorDriver::KeepClocksOnSleep(const Writer &writer) const
{
    if (!NorManagement::Instance().Validate(writer).Ok()) {
        return;
    }
    __HAL_RCC_XSPI2_CLK_SLEEP_ENABLE();
}

} // namespace uai::ai::nor
