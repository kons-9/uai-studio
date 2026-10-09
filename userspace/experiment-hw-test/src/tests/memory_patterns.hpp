#pragma once

#include "board.hpp"

namespace experiment::hwtest::tests {

inline Result CheckMemoryPatterns(std::uint8_t *buffer, std::size_t size, bool flush_cache)
{
    volatile std::uint8_t *data = buffer;
    for (unsigned pattern = 0; pattern < 4; ++pattern) {
        for (std::size_t index = 0; index < size; ++index) {
            data[index] = static_cast<std::uint8_t>((index * 37U) ^ (0x55U * pattern));
        }
        if (flush_cache) { SCB_CleanInvalidateDCache_by_Addr(buffer, static_cast<std::int32_t>(size)); }
        for (std::size_t index = 0; index < size; ++index) {
            if (data[index] != static_cast<std::uint8_t>((index * 37U) ^ (0x55U * pattern))) {
                return {Outcome::kFail, "pattern-mismatch"};
            }
        }
    }
    return {Outcome::kPass, flush_cache ? "patterns-after-cache-clean-invalidate" : "cpu-sram-patterns"};
}

}