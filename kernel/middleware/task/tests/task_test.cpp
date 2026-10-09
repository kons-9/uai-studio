#include "middleware/task/task.hpp"

#include <gtest/gtest.h>

namespace uai::ai::common {
namespace {

void Entry(
    INT,
    void *
)
{}

struct Monitor {
    common::Error RegisterTask(
        ID,
        const char *
    )
    {
        return {};
    }
    std::uint32_t BeginTaskLoop() const { return 0U; }
    void RecordTaskLoop(
        ID,
        std::uint32_t
    )
    {}
};

TEST(
    TaskTest,
    StartsWithCallerOwnedStack
)
{
    common::StableAlignedBytes<128U> stack;
    Monitor monitor;
    Task::Start(monitor, Entry, stack, 5, "worker");
    EXPECT_EQ(Task::Now(), 0U);
}

TEST(
    TaskTest,
    WaitsBeforeProcessing
)
{
    struct LoopExit {};
    Monitor monitor;
    unsigned int waits = 0U;
    unsigned int processed = 0U;
    EXPECT_THROW(
        Task::RunForever(
            monitor,
            "worker",
            [&] {
                ++waits;
            },
            [&] {
                ++processed;
                throw LoopExit{};
            }
        ),
        LoopExit
    );
    EXPECT_EQ(waits, 1U);
    EXPECT_EQ(processed, 1U);
}

} // namespace
} // namespace uai::ai::common