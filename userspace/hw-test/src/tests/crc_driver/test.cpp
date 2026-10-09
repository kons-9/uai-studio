#include "hwtest.hpp"
#include "driver/peripheral_driver/peripheral_driver.hpp"

namespace experiment::hwtest::tests::crc_driver {

Result Run(const Context &)
{
    alignas(4) static std::uint8_t input[12] = "123456789";
    uai::ai::peripheral::PeripheralManagement::Accessor accessor;
    if (!uai::ai::peripheral::PeripheralManagement::Instance().Acquire(&accessor, 100).Ok()) {
        return {Outcome::kFail, "crc-ownership"};
    }
    std::uint32_t first = 0, second = 0;
    const uai::ai::peripheral::Memory memory{input, sizeof(input)};
    if (!accessor->Crc32Mpeg2(memory, 9, &first, accessor.Ownership()).Ok()
        || !accessor->Crc32Mpeg2(memory, 9, &second, accessor.Ownership()).Ok()) {
        return {Outcome::kFail, "crc-calculation"};
    }
    return first == 0x0376e6e7U && second == first ? Result{Outcome::kPass, "crc32-known-vector-and-reset"}
                                                   : Result{Outcome::kFail, "crc32-known-vector-mismatch"};
}

}