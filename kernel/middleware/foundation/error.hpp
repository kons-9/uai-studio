#ifndef UAI_AI_COMMON_ERROR_HPP
#define UAI_AI_COMMON_ERROR_HPP

#include <cstdint>

#include "middleware/foundation/log.hpp"

namespace uai::ai::common {

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
    kOwnership,
    kInvalidState,
    kBufferOverflow,
};

constexpr const char *ErrorCodeName(ErrorCode code)
{
    switch (code) {
    case ErrorCode::kOk: return "ok";
    case ErrorCode::kInvalidArgument: return "invalid_argument";
    case ErrorCode::kNotInitialized: return "not_initialized";
    case ErrorCode::kAlreadyInitialized: return "already_initialized";
    case ErrorCode::kHardware: return "hardware";
    case ErrorCode::kCache: return "cache";
    case ErrorCode::kNoFrame: return "no_frame";
    case ErrorCode::kNoBuffer: return "no_buffer";
    case ErrorCode::kBufferOverflow: return "buffer_overflow";
    case ErrorCode::kQueueFull: return "queue_full";
    case ErrorCode::kTimeout: return "timeout";
    case ErrorCode::kModel: return "model";
    case ErrorCode::kNpu: return "npu";
    case ErrorCode::kOwnership: return "ownership";
    case ErrorCode::kInvalidState: return "invalid_state";
    }
    return "unknown";
}

struct Error {
    ErrorCode code = ErrorCode::kOk;
    std::uint32_t detail = 0U;
    const char *operation = "ok";

    constexpr bool Ok() const { return code == ErrorCode::kOk; }
    constexpr bool IsRoutine() const
    {
        return code == ErrorCode::kNoFrame ||
               code == ErrorCode::kNoBuffer ||
               code == ErrorCode::kQueueFull;
    }
    void LogStatus(const char *component) const
    {
        if (Ok()) return;

        const LogLevel level = IsRoutine() ? LogLevel::kDebug : LogLevel::kError;
        UAI_LOGF(level, "error: component=%s operation=%s code=%s(%u) detail=%x\n",
                 component, operation, ErrorCodeName(code),
                 static_cast<unsigned int>(code),
                 static_cast<unsigned int>(detail));
    }
};

} // namespace uai::ai::common

#endif