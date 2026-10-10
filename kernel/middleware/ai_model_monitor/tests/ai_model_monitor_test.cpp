#include "middleware/ai_model_monitor/ai_model_monitor.hpp"

#include <gtest/gtest.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#include "middleware/memory/static_memory_layout.hpp"

using namespace uai::ai;
using namespace uai::ai::middleware::ai_model_monitor;

namespace {

alignas(32) std::uint8_t trace_memory[65536]{};
std::mutex interrupt_mutex;
thread_local std::uint32_t interrupt_mask = 0U;
unsigned int monitor_delays = 0U;

template <typename Predicate>
bool WaitUntil(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::yield();
    }
    return true;
}

bool WriteDumpIfRequested()
{
    const char *path = std::getenv("UAI_AI_TRACE_TEST_DUMP");
    if (path == nullptr)
        return true;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char *>(trace_memory), sizeof(trace_memory));
    return output.good();
}

void RunMonitorCycle()
{
    struct CycleComplete {};
    monitor_delays = 0U;
    uai::test::kernel::delay_hook = [](RELTIM) {
        if (++monitor_delays == 2U)
            throw CycleComplete{};
    };
    const T_CTSK task = uai::test::kernel::created_task;
    try {
        task.task(0, task.exinf);
    } catch (const CycleComplete &) {
    }
    uai::test::kernel::delay_hook = nullptr;
}

std::vector<ThreadMonitorTraceRecord> StepRecords()
{
    const auto *header = reinterpret_cast<const ThreadMonitorTraceHeader *>(trace_memory);
    const auto *records =
        reinterpret_cast<const ThreadMonitorTraceRecord *>(trace_memory + kThreadMonitorTraceDataOffset);
    std::vector<ThreadMonitorTraceRecord> result;
    for (std::uint32_t index = 0U; index < header->record_count; ++index) {
        if (records[index].type == static_cast<std::uint8_t>(TraceRecordType::kAiRuntimeStep)) {
            result.push_back(records[index]);
        }
    }
    return result;
}

ai_runtime::StepTrace Step(
    std::uint32_t inference_id,
    std::uint32_t timestamp,
    bool begin
)
{
    return {
        inference_id, static_cast<ai_runtime::AiModelId>(1U), 1U, ai_runtime::ExecutionContext::kNpu, timestamp, begin
    };
}

} // namespace

extern "C" std::uint32_t __get_PRIMASK()
{
    return interrupt_mask;
}

extern "C" void __disable_irq()
{
    if (interrupt_mask == 0U)
        interrupt_mutex.lock();
    interrupt_mask = 1U;
}

extern "C" void __set_PRIMASK(std::uint32_t primask)
{
    const bool enable = interrupt_mask != 0U && primask == 0U;
    interrupt_mask = primask;
    if (enable)
        interrupt_mutex.unlock();
}

namespace uai::ai::static_memory_layout {

const Region Region::GetRegionFromKey(Key)
{
    return {trace_memory, trace_memory + sizeof(trace_memory)};
}

} // namespace uai::ai::static_memory_layout

TEST(
    AiModelMonitor,
    InterleavedDurations
)
{
    std::memset(trace_memory, 0, sizeof(trace_memory));
    AiModelMonitor monitor;
    ASSERT_TRUE(monitor.Start().Ok());
    monitor.ObserveAiRuntimeStep(Step(1U, 10U, true));
    monitor.ObserveAiRuntimeStep(Step(2U, 20U, true));
    monitor.ObserveAiRuntimeStep(Step(1U, 40U, false));
    monitor.ObserveAiRuntimeStep(Step(2U, 50U, false));
    ASSERT_TRUE(monitor.Stop().Ok());

    const auto *header = reinterpret_cast<const ThreadMonitorTraceHeader *>(trace_memory);
    const auto *records =
        reinterpret_cast<const ThreadMonitorTraceRecord *>(trace_memory + kThreadMonitorTraceDataOffset);
    ASSERT_EQ(header->record_count, 4U);
    EXPECT_EQ(records[2].npu_elapsed_ms, 30U);
    EXPECT_EQ(records[3].npu_elapsed_ms, 30U);
}

TEST(
    AiModelMonitor,
    TraceIsStableWhilePausedAndResumes
)
{
    std::memset(trace_memory, 0, sizeof(trace_memory));
    AiModelMonitor monitor;
    ASSERT_TRUE(monitor.Start().Ok());
    RunMonitorCycle();
    const auto *header = reinterpret_cast<const ThreadMonitorTraceHeader *>(trace_memory);
    const std::uint32_t before = header->record_count;
    ASSERT_TRUE(monitor.PauseTrace().Ok());
    monitor.ObserveAiRuntimeStep(Step(1U, 20U, true));
    RunMonitorCycle();
    EXPECT_EQ(header->record_count, before);
    monitor.ResumeTrace();
    RunMonitorCycle();
    EXPECT_GT(header->record_count, before);
    EXPECT_TRUE(monitor.Stop().Ok());
}

TEST(
    AiModelMonitor,
    RecoversTimingAfterUnmatchedBegins
)
{
    std::memset(trace_memory, 0, sizeof(trace_memory));
    AiModelMonitor monitor;
    ASSERT_TRUE(monitor.Start().Ok());
    for (std::uint32_t inference_id = 1U; inference_id <= 17U; ++inference_id) {
        monitor.ObserveAiRuntimeStep(Step(inference_id, inference_id, true));
    }
    monitor.ObserveAiRuntimeStep(Step(17U, 47U, false));
    monitor.ObserveAiRuntimeStep(Step(1U, 48U, false));
    ASSERT_TRUE(monitor.Stop().Ok());

    const auto *records =
        reinterpret_cast<const ThreadMonitorTraceRecord *>(trace_memory + kThreadMonitorTraceDataOffset);
    EXPECT_EQ(records[17].npu_elapsed_ms, 30U);
    EXPECT_NE(records[17].flags & kTraceFlagTimingValid, 0U);
    EXPECT_EQ(records[18].flags & kTraceFlagTimingValid, 0U);
}

TEST(
    AiModelMonitor,
    ConcurrentProducers
)
{
    std::memset(trace_memory, 0, sizeof(trace_memory));
    AiModelMonitor monitor;
    ASSERT_TRUE(monitor.Start().Ok());
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    std::atomic<bool> synchronization_failed{false};
    const auto produce = [&](std::uint32_t first_id) {
        ++ready;
        if (!WaitUntil([&] {
                return go.load();
            })) {
            synchronization_failed = true;
            return;
        }
        for (std::uint32_t index = 0U; index < 40U; ++index) {
            monitor.ObserveAiRuntimeStep(Step(first_id + index, index, true));
        }
    };
    std::thread first(produce, 1U);
    std::thread second(produce, 41U);
    const bool producers_ready = WaitUntil([&] {
        return ready.load() == 2;
    });
    go = true;
    first.join();
    second.join();
    ASSERT_TRUE(monitor.Stop().Ok());
    EXPECT_TRUE(producers_ready);
    EXPECT_FALSE(synchronization_failed.load());
    const auto *header = reinterpret_cast<const ThreadMonitorTraceHeader *>(trace_memory);
    EXPECT_EQ(header->record_count, 80U);
    EXPECT_EQ(header->dropped_count, 0U);
}

TEST(
    AiModelMonitor,
    MissingBeginIsNotValidTiming
)
{
    std::memset(trace_memory, 0, sizeof(trace_memory));
    AiModelMonitor monitor;
    ASSERT_TRUE(monitor.Start().Ok());
    for (std::uint32_t inference_id = 1U; inference_id <= 127U; ++inference_id) {
        monitor.ObserveAiRuntimeStep(Step(inference_id, inference_id, true));
    }
    monitor.ObserveAiRuntimeStep(Step(128U, 1000U, true));
    RunMonitorCycle();
    monitor.ObserveAiRuntimeStep(Step(128U, 1030U, false));
    ASSERT_TRUE(monitor.Stop().Ok());

    const auto *header = reinterpret_cast<const ThreadMonitorTraceHeader *>(trace_memory);
    const auto records = StepRecords();
    ASSERT_EQ(records.size(), 128U);
    EXPECT_EQ(header->dropped_count, 1U);
    EXPECT_EQ(records[127].npu_elapsed_ms, 0U);
    EXPECT_EQ(records[127].flags & kTraceFlagTimingValid, 0U);
}

TEST(
    AiModelMonitor,
    ZeroDurationAndTimestampWrapAreValid
)
{
    std::memset(trace_memory, 0, sizeof(trace_memory));
    AiModelMonitor monitor;
    ASSERT_TRUE(monitor.Start().Ok());
    monitor.ObserveAiRuntimeStep(Step(1U, 100U, true));
    RunMonitorCycle();
    monitor.ObserveAiRuntimeStep(Step(1U, 100U, false));
    monitor.ObserveAiRuntimeStep(Step(2U, UINT32_MAX - 9U, true));
    RunMonitorCycle();
    monitor.ObserveAiRuntimeStep(Step(2U, 20U, false));
    ASSERT_TRUE(monitor.Stop().Ok());

    const auto records = StepRecords();
    ASSERT_EQ(records.size(), 4U);
    EXPECT_EQ(records[1].npu_elapsed_ms, 0U);
    EXPECT_NE(records[1].flags & kTraceFlagTimingValid, 0U);
    EXPECT_EQ(records[3].npu_elapsed_ms, 30U);
    EXPECT_NE(records[3].flags & kTraceFlagTimingValid, 0U);
}

TEST(
    AiModelMonitor,
    ConcurrentProducersAndConsumerAcrossWraps
)
{
    std::memset(trace_memory, 0, sizeof(trace_memory));
    AiModelMonitor monitor;
    ASSERT_TRUE(monitor.Start().Ok());
    std::atomic<unsigned int> completed_batches{0U};
    std::atomic<unsigned int> released_batches{0U};
    std::atomic<bool> synchronization_failed{false};
    const auto produce = [&](std::uint32_t first_id) {
        for (std::uint32_t batch = 0U; batch < 16U; ++batch) {
            for (std::uint32_t offset = 0U; offset < 16U; ++offset) {
                const std::uint32_t inference_id = first_id + batch * 16U + offset;
                monitor.ObserveAiRuntimeStep(Step(inference_id, inference_id, true));
            }
            ++completed_batches;
            if (!WaitUntil([&] {
                    return released_batches.load() > batch;
                })) {
                synchronization_failed = true;
                return;
            }
        }
    };
    std::thread first(produce, 1U);
    std::thread second(produce, 257U);
    for (unsigned int batch = 0U; batch < 16U; ++batch) {
        RunMonitorCycle();
        if (!WaitUntil([&] {
                return completed_batches.load() >= 2U * (batch + 1U);
            })) {
            synchronization_failed = true;
            break;
        }
        RunMonitorCycle();
        ++released_batches;
    }
    released_batches = 16U;
    first.join();
    second.join();
    ASSERT_TRUE(monitor.Stop().Ok());
    EXPECT_FALSE(synchronization_failed.load());

    const auto *header = reinterpret_cast<const ThreadMonitorTraceHeader *>(trace_memory);
    const auto records = StepRecords();
    ASSERT_EQ(records.size(), 512U);
    EXPECT_EQ(header->dropped_count, 0U);
    std::array<bool, 513U> seen{};
    for (std::size_t index = 0U; index < records.size(); ++index) {
        const std::uint32_t inference_id = records[index].task_state;
        ASSERT_GT(inference_id, 0U);
        ASSERT_LT(inference_id, seen.size());
        EXPECT_FALSE(seen[inference_id]);
        seen[inference_id] = true;
    }
}

TEST(
    AiModelMonitor,
    FullQueueRejectsMessagesAcrossWraps
)
{
    std::memset(trace_memory, 0, sizeof(trace_memory));
    AiModelMonitor monitor;
    ASSERT_TRUE(monitor.Start().Ok());
    for (std::uint32_t cycle = 0U; cycle < 6U; ++cycle) {
        for (std::uint32_t offset = 0U; offset < 127U; ++offset) {
            const std::uint32_t inference_id = cycle * 127U + offset + 1U;
            monitor.ObserveAiRuntimeStep(Step(inference_id, inference_id, true));
        }
        monitor.ObserveAiRuntimeStep(Step(9999U, 9999U, true));
        RunMonitorCycle();
    }
    ASSERT_TRUE(monitor.Stop().Ok());

    const auto *header = reinterpret_cast<const ThreadMonitorTraceHeader *>(trace_memory);
    const auto records = StepRecords();
    ASSERT_EQ(records.size(), 762U);
    EXPECT_EQ(header->dropped_count, 6U);
    for (std::size_t index = 0U; index < records.size(); ++index) {
        EXPECT_EQ(records[index].task_state, index + 1U);
    }
    EXPECT_TRUE(WriteDumpIfRequested());
}