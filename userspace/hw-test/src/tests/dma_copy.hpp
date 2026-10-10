#pragma once

#include "tests/framework.hpp"
#include "driver/peripheral_driver/peripheral_driver.hpp"

namespace uai::hwtest::tests {

inline Result CheckDmaCopy(
    uai::ai::peripheral::DmaController controller,
    const Context &context
)
{
    uai::ai::peripheral::PeripheralManagement::Accessor accessor;
    if (!uai::ai::peripheral::PeripheralManagement::Instance().Acquire(&accessor, 100).Ok()) {
        return {Outcome::kFail, "dma-ownership"};
    }
    alignas(32) static std::uint8_t source[320];
    alignas(32) static std::uint8_t destination[320];
    static constexpr std::size_t lengths[] = {1, 3, 31, 32, 33, 255, 256};
    for (std::size_t scenario = 0; scenario < sizeof(lengths) / sizeof(lengths[0]); ++scenario) {
        const auto length = lengths[scenario];
        const std::size_t offset = length == 256 ? 32 : 33;
        for (std::size_t index = 0; index < sizeof(source); ++index) {
            source[index] = static_cast<std::uint8_t>((index * 37U) ^ 0xa5U);
            destination[index] = 0x5a;
        }
        if (!accessor
                 ->Copy(
                     controller,
                     {source, sizeof(source)},
                     {destination, sizeof(destination)},
                     offset,
                     length,
                     100,
                     accessor.Ownership()
                 )
                 .Ok()) {
            return {Outcome::kFail, "dma-transfer-or-timeout"};
        }
        for (std::size_t index = 0; index < sizeof(source); ++index) {
            const auto original = static_cast<std::uint8_t>((index * 37U) ^ 0xa5U);
            const auto expected = index >= offset && index < offset + length ? original : 0x5a;
            if (source[index] != original || destination[index] != expected) {
                return {Outcome::kFail, "dma-data-or-guard-mismatch"};
            }
        }
        context.Progress(
            static_cast<unsigned>(scenario + 1), static_cast<unsigned>(sizeof(lengths) / sizeof(lengths[0]))
        );
    }
    return {Outcome::kPass, "seven-copy-lengths-guards-cache"};
}

}