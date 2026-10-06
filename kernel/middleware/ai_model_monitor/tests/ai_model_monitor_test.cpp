#include "middleware/ai_model_monitor/ai_model_monitor.hpp"

#include <gtest/gtest.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>

#include "middleware/memory/static_memory_layout.hpp"

using namespace uai::ai;
using namespace uai::ai::middleware::ai_model_monitor;

namespace {

alignas(32) std::uint8_t trace_memory[8192]{};

ai_runtime::StepTrace Step(std::uint32_t inference_id,
                           std::uint32_t timestamp, bool begin)
{
    return {inference_id, static_cast<ai_runtime::AiModelId>(1U), 1U,
            ai_runtime::ExecutionContext::kNpu, timestamp, begin};
}

} // namespace

namespace uai::ai::static_memory_layout {

const Region Region::GetRegionFromKey(Key)
{
    return {trace_memory, trace_memory + sizeof(trace_memory)};
}

} // namespace uai::ai::static_memory_layout

TEST(AiModelMonitor, InterleavedDurations)
{
    std::memset(trace_memory, 0, sizeof(trace_memory));
    AiModelMonitor monitor;
    ASSERT_TRUE(monitor.Start().Ok());
    monitor.ObserveAiRuntimeStep(Step(1U, 10U, true));
    monitor.ObserveAiRuntimeStep(Step(2U, 20U, true));
    monitor.ObserveAiRuntimeStep(Step(1U, 40U, false));
    monitor.ObserveAiRuntimeStep(Step(2U, 50U, false));
    ASSERT_TRUE(monitor.Stop().Ok());

    const auto *header = reinterpret_cast<const ThreadMonitorTraceHeader *>(
        trace_memory);
    const auto *records = reinterpret_cast<const ThreadMonitorTraceRecord *>(
        trace_memory + kThreadMonitorTraceDataOffset);
    ASSERT_EQ(header->record_count, 4U);
    EXPECT_EQ(records[2].npu_elapsed_ms, 30U);
    EXPECT_EQ(records[3].npu_elapsed_ms, 30U);
}

TEST(AiModelMonitor, ConcurrentProducers)
{
    std::memset(trace_memory, 0, sizeof(trace_memory));
    AiModelMonitor monitor;
    ASSERT_TRUE(monitor.Start().Ok());
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    const auto produce = [&](std::uint32_t first_id) {
        ++ready;
        while (!go.load()) std::this_thread::yield();
        for (std::uint32_t index = 0U; index < 40U; ++index) {
            monitor.ObserveAiRuntimeStep(Step(first_id + index, index, true));
        }
    };
    std::thread first(produce, 1U);
    std::thread second(produce, 41U);
    while (ready.load() != 2) std::this_thread::yield();
    go = true;
    first.join();
    second.join();
    ASSERT_TRUE(monitor.Stop().Ok());
    const auto *header = reinterpret_cast<const ThreadMonitorTraceHeader *>(
        trace_memory);
    EXPECT_EQ(header->record_count, 80U);
    EXPECT_EQ(header->dropped_count, 0U);
}