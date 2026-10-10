#pragma once

#include "board.hpp"
#include "driver/cache_driver/cache_driver.hpp"

namespace uai::hwtest::tests {

inline Result CheckMemoryPatterns(
    std::uint8_t *buffer,
    std::size_t size,
    bool flush_cache
)
{
    volatile std::uint8_t *data = buffer;
    for (unsigned pattern = 0; pattern < 4; ++pattern) {
        for (std::size_t index = 0; index < size; ++index) {
            data[index] = static_cast<std::uint8_t>((index * 37U) ^ (0x55U * pattern));
        }
        if (flush_cache) {
            if (!uai::ai::cache::CacheDriver::CleanInvalidate(buffer, size).Ok()) {
                return {Outcome::kFail, "memory-cache-synchronization"};
            }
        }
        for (std::size_t index = 0; index < size; ++index) {
            if (data[index] != static_cast<std::uint8_t>((index * 37U) ^ (0x55U * pattern))) {
                return {Outcome::kFail, "pattern-mismatch"};
            }
        }
    }
    return {Outcome::kPass, flush_cache ? "patterns-after-cache-clean-invalidate" : "cpu-sram-patterns"};
}

}