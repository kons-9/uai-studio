#pragma once

#include "middleware/foundation/error_code.hpp"

namespace uai::ai::common {

enum class LogLevel : std::uint8_t;

class Error {
public:
    constexpr Error(ErrorCode code = ErrorCode::kOk) : code_(code) {}

    constexpr ErrorCode Code() const { return code_; }

    constexpr bool Ok() const { return code_ == ErrorCode::kOk; }
    constexpr bool IsRoutine() const
    {
        return code_ == ErrorCode::kNoFrame || code_ == ErrorCode::kNoBuffer || code_ == ErrorCode::kQueueFull;
    }
    /* Routine codes log at Debug, all others at Error. */
    void LogStatus(const char *component) const;
    void LogStatus(
        const char *component,
        LogLevel level
    ) const;

private:
    ErrorCode code_;
};

} // namespace uai::ai::common
