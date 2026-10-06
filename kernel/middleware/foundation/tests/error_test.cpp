#include "middleware/foundation/error.hpp"
#include "middleware/foundation/log.hpp"

#include <cstdarg>
#include <cstdio>
#include <string>

#include <gtest/gtest.h>

namespace {
std::string captured_log;
}

extern "C" int tm_printf(const UB *format, ...)
{
    char text[512];
    va_list arguments;
    va_start(arguments, format);
    const int result = std::vsnprintf(
        text, sizeof(text), reinterpret_cast<const char *>(format), arguments);
    va_end(arguments);
    captured_log += text;
    return result;
}

namespace uai::ai::common {
namespace {

struct CodeContract {
    ErrorCode code;
    unsigned int value;
    const char *name;
    bool routine;
};

constexpr CodeContract codes[] = {
    {ErrorCode::kOk, 0U, "ok", false},
    {ErrorCode::kInvalidArgument, 1U, "invalid_argument", false},
    {ErrorCode::kNotInitialized, 2U, "not_initialized", false},
    {ErrorCode::kAlreadyInitialized, 3U, "already_initialized", false},
    {ErrorCode::kHardware, 4U, "hardware", false},
    {ErrorCode::kCache, 5U, "cache", false},
    {ErrorCode::kNoFrame, 6U, "no_frame", true},
    {ErrorCode::kNoBuffer, 7U, "no_buffer", true},
    {ErrorCode::kQueueFull, 8U, "queue_full", true},
    {ErrorCode::kTimeout, 9U, "timeout", false},
    {ErrorCode::kModel, 10U, "model", false},
    {ErrorCode::kNpu, 11U, "npu", false},
    {ErrorCode::kOwnership, 12U, "ownership", false},
    {ErrorCode::kInvalidState, 13U, "invalid_state", false},
    {ErrorCode::kBufferOverflow, 14U, "buffer_overflow", false},
};

static_assert(kLogLevel == LogLevel::UAI_EXPECT_LOG_LEVEL);

TEST(ErrorTest, PreservesValueContract)
{
    constexpr Error status;
    EXPECT_EQ(status.Code(), ErrorCode::kOk);
    for (const auto &entry : codes) {
        EXPECT_EQ(static_cast<unsigned int>(entry.code), entry.value);
        EXPECT_STREQ(ErrorCodeName(entry.code), entry.name);
        EXPECT_EQ((Error{entry.code}).Ok(), entry.code == ErrorCode::kOk);
        EXPECT_EQ((Error{entry.code}).IsRoutine(), entry.routine);
    }
    EXPECT_STREQ(ErrorCodeName(static_cast<ErrorCode>(255U)), "unknown");
}

TEST(ErrorTest, PreservesLoggingContractForEveryCode)
{
    for (const auto &entry : codes) {
        SCOPED_TRACE(entry.name);
        captured_log.clear();
        (Error{entry.code}).LogStatus("test");
        if (entry.code == ErrorCode::kOk ||
            (entry.routine && !UAI_EXPECT_ROUTINE_LOG)) {
            EXPECT_TRUE(captured_log.empty());
        } else {
            EXPECT_EQ(captured_log,
                      "error: component=test code=" +
                          std::string(entry.name) + "(" +
                          std::to_string(entry.value) + ")\n");
        }
    }
}

TEST(ErrorTest, LogsAtCallerSelectedLevel)
{
    const Error routine{ErrorCode::kNoFrame};
    captured_log.clear();
    routine.LogStatus("test", LogLevel::kError);
    EXPECT_EQ(captured_log,
              "error: component=test code=no_frame(6)\n");

    const Error failure{ErrorCode::kHardware};
    captured_log.clear();
    failure.LogStatus("test", LogLevel::kTrace);
    EXPECT_TRUE(captured_log.empty());

    captured_log.clear();
    Error{}.LogStatus("test", LogLevel::kError);
    EXPECT_TRUE(captured_log.empty());
}

} // namespace
} // namespace uai::ai::common