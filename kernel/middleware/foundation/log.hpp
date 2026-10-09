#pragma once

#include <cstdint>

extern "C" {
#include <tm/tmonitor.h>
}

namespace uai::ai::common {

/*
 * Lower values are more important.  Keep the default at WARN so that the
 * normal camera/inference loop does not spend time writing diagnostics over
 * the low-bandwidth T-Monitor UART.  Raise this value temporarily while
 * investigating a board:
 *
 *   kError -> failures only
 *   kWarn  -> failures and recoverable hardware warnings (default)
 *   kInfo  -> boot/model lifecycle messages
 *   kDebug -> input and timing diagnostics
 *   kTrace -> per-frame trace messages
 */
enum class LogLevel : std::uint8_t {
    kError = 0U,
    kWarn = 1U,
    kInfo = 2U,
    kDebug = 3U,
    kTrace = 4U,
};

/* Temporarily enabled for NPU timing investigation.  Per-inference timing
 * output is throttled in AiRuntime so it does not flood the UART. */
inline constexpr LogLevel kLogLevel = LogLevel::kInfo;

constexpr bool IsLogEnabled(LogLevel level)
{
    return static_cast<std::uint8_t>(level) <= static_cast<std::uint8_t>(kLogLevel);
}

inline const UB *ToMonitorText(const char *text)
{
    static_assert(sizeof(UB) == sizeof(char));
    return reinterpret_cast<const UB *>(text);
}

const UB *ToMonitorText(const UB *) = delete;

} // namespace uai::ai::common

/*
 * tm_printf has no va_list variant in this BSP.  Keeping the level check in
 * the macro means disabled messages do not call into T-Monitor and their
 * arguments are not evaluated at runtime.
 */
#define UAI_LOGF(level, format, ...)                                                                                   \
    do {                                                                                                               \
        if (::uai::ai::common::IsLogEnabled(level)) {                                                                  \
            tm_printf(::uai::ai::common::ToMonitorText(format), ##__VA_ARGS__);                                        \
        }                                                                                                              \
    } while (false)

#define UAI_LOG_TEXT(level, text)                                                                                      \
    do {                                                                                                               \
        if (::uai::ai::common::IsLogEnabled(level)) {                                                                  \
            tm_putstring(::uai::ai::common::ToMonitorText(text));                                                      \
        }                                                                                                              \
    } while (false)

#define UAI_LOG_ERROR(format, ...) UAI_LOGF(::uai::ai::common::LogLevel::kError, format, ##__VA_ARGS__)
#define UAI_LOG_WARN(format, ...) UAI_LOGF(::uai::ai::common::LogLevel::kWarn, format, ##__VA_ARGS__)
#define UAI_LOG_INFO(format, ...) UAI_LOGF(::uai::ai::common::LogLevel::kInfo, format, ##__VA_ARGS__)
#define UAI_LOG_DEBUG(format, ...) UAI_LOGF(::uai::ai::common::LogLevel::kDebug, format, ##__VA_ARGS__)
#define UAI_LOG_TRACE(format, ...) UAI_LOGF(::uai::ai::common::LogLevel::kTrace, format, ##__VA_ARGS__)
