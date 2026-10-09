#include "tests/memory_patterns.hpp"
#include "driver/psram_driver/psram_driver.hpp"

namespace experiment::hwtest::tests::psram_driver {
namespace {
alignas(32) __attribute__((section(".experiment_scratch"))) std::uint8_t scratch[4096];
}

Result Run(const Context &)
{
    auto &driver = uai::ai::psram::PsramManagement::Instance();
    if (!driver.Initialize())
        return {Outcome::kFail, "psram-initialization-or-mapping"};
    uai::ai::psram::PsramManagement::Accessor accessor;
    if (!driver.Acquire(&accessor, 100).Ok())
        return {Outcome::kFail, "psram-ownership"};
    return CheckMemoryPatterns(scratch, sizeof(scratch), true);
}

}