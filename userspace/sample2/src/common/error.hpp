#ifndef UAI_SAMPLE2_COMMON_ERROR_HPP
#define UAI_SAMPLE2_COMMON_ERROR_HPP

#include <cstdint>

namespace uai::sample2::common {

enum class ErrorCode : std::uint8_t {
    kOk,
    kInvalidArgument,
    kNotInitialized,
    kAlreadyInitialized,
    kHardware,
    kCache,
    kNoFrame,
    kNoBuffer,
    kQueueFull,
    kTimeout,
    kModel,
    kNpu,
};

struct Error {
    ErrorCode code = ErrorCode::kOk;
    std::uint32_t detail = 0U;
    const char *operation = "ok";

    constexpr bool Ok() const { return code == ErrorCode::kOk; }
};

} // namespace uai::sample2::common

#endif
