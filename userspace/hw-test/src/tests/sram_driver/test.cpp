#include "tests/memory_patterns.hpp"

namespace experiment::hwtest::tests::sram_driver {

Result Run(const Context &)
{
    alignas(32) static std::uint8_t scratch[4096];
    return CheckMemoryPatterns(scratch, sizeof(scratch), false);
}

}