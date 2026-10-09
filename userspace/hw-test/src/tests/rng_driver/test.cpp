#include "hwtest.hpp"
#include "driver/peripheral_driver/peripheral_driver.hpp"

namespace experiment::hwtest::tests::rng_driver {

Result Run(const Context &)
{
    uai::ai::peripheral::PeripheralManagement::Accessor accessor;
    if (!uai::ai::peripheral::PeripheralManagement::Instance().Acquire(&accessor, 100).Ok()) {
        return {Outcome::kFail, "rng-ownership"};
    }
    std::uint32_t words[64]{};
    if (!accessor->RandomWords(words, 64, 500, accessor.Ownership()).Ok()) {
        return {Outcome::kFail, "rng-read-or-hardware-error"};
    }
    for (const auto word : words) {
        if (word != words[0])
            return {Outcome::kPass, "64-words-no-hardware-error"};
    }
    return {Outcome::kFail, "rng-stuck-output"};
}

}