#include "middleware/foundation/error.hpp"

#include <gtest/gtest.h>

namespace uai::ai::common {
namespace {

TEST(ErrorTest, RoutineStatusesAreNotLoggedAsErrors)
{
    EXPECT_TRUE((Error{ErrorCode::kNoFrame}).IsRoutine());
    EXPECT_TRUE((Error{ErrorCode::kNoBuffer}).IsRoutine());
    EXPECT_TRUE((Error{ErrorCode::kQueueFull}).IsRoutine());
    EXPECT_FALSE((Error{ErrorCode::kHardware}).IsRoutine());
    EXPECT_FALSE((Error{ErrorCode::kBufferOverflow}).IsRoutine());
    (Error{ErrorCode::kNoFrame, 0U, "receive"}).LogStatus("test");
    (Error{ErrorCode::kHardware, 1U, "read"}).LogStatus("test");
}

} // namespace
} // namespace uai::ai::common