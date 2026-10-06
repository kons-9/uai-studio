#ifndef UAI_AI_COMMON_ERROR_CODE_HPP
#define UAI_AI_COMMON_ERROR_CODE_HPP

#include <cstdint>

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

} // namespace uai::ai::common

#endif