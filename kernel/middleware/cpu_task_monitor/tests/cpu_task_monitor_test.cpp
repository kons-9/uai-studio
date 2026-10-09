#include "middleware/cpu_task_monitor/cpu_task_monitor.hpp"

#include <cstdlib>
#include <cstring>
#include <fstream>

#include <gtest/gtest.h>

#include "middleware/memory/static_memory_layout.hpp"
#include "middleware/memory/generated/static_memory_layout/key.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
}

namespace {

alignas(32) std::uint8_t trace_memory[8192]{};
TD_HDSP dispatch_hook{};
TD_HINT interrupt_hook{};
std::uint32_t interrupt_mask = 0U;

bool WriteDumpIfRequested()
{
    const char *path = std::getenv("UAI_CPU_TRACE_TEST_DUMP");
    if (path == nullptr)
        return true;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char *>(trace_memory), sizeof(trace_memory));
    return output.good();
}

} // namespace

UaiTestDwt uai_test_dwt{};
UaiTestCoreDebug uai_test_core_debug{};

extern "C" std::uint32_t __get_PRIMASK()
{
    return interrupt_mask;
}
extern "C" void __disable_irq()
{
    interrupt_mask = 1U;
}
extern "C" void __enable_irq()
{
    interrupt_mask = 0U;
}
extern "C" void SCB_CleanDCache_by_Addr(
    std::uint32_t *,
    std::int32_t
)
{}

extern "C" ER td_hok_dsp(const TD_HDSP *hook)
{
    dispatch_hook = hook == nullptr ? TD_HDSP{} : *hook;
    return E_OK;
}

extern "C" ER td_hok_int(const TD_HINT *hook)
{
    interrupt_hook = hook == nullptr ? TD_HINT{} : *hook;
    return E_OK;
}

namespace uai::ai::static_memory_layout {

const Region Region::GetRegionFromKey(Key)
{
    return {trace_memory, trace_memory + sizeof(trace_memory)};
}

} // namespace uai::ai::static_memory_layout

namespace uai::ai::middleware::cpu_task_monitor {
namespace {

TEST(
    CpuTaskMonitor,
    HooksAccountTaskInterruptAndLoopRecords
)
{
    std::memset(trace_memory, 0, sizeof(trace_memory));
    uai_test_dwt = {};
    uai_test_core_debug = {};
    dispatch_hook = {};
    interrupt_hook = {};
    CpuTaskMonitor monitor;
    ASSERT_TRUE(monitor.InitializeTraceBuffer().Ok());
    ASSERT_TRUE(monitor.RegisterTask(2, "worker").Ok());
    uai_test_dwt.CYCCNT = 100U;
    ASSERT_TRUE(monitor.Start().Ok());
    ASSERT_NE(dispatch_hook.exec, nullptr);
    ASSERT_NE(dispatch_hook.stop, nullptr);
    ASSERT_NE(interrupt_hook.enter, nullptr);
    ASSERT_NE(interrupt_hook.leave, nullptr);

    const auto dispatch_exec = reinterpret_cast<void (*)(ID, ID)>(dispatch_hook.exec);
    const auto dispatch_stop = reinterpret_cast<void (*)(ID, ID, UINT)>(dispatch_hook.stop);
    const auto interrupt_enter = reinterpret_cast<void (*)(UINT)>(interrupt_hook.enter);
    const auto interrupt_leave = reinterpret_cast<void (*)(UINT)>(interrupt_hook.leave);

    uai_test_dwt.CYCCNT = 110U;
    dispatch_exec(2, 0);
    uai_test_dwt.CYCCNT = 120U;
    interrupt_enter(3U);
    uai_test_dwt.CYCCNT = 130U;
    interrupt_leave(3U);
    uai_test_dwt.CYCCNT = 150U;
    dispatch_stop(2, 0, 7U);
    const std::uint32_t loop_start = monitor.BeginTaskLoop();
    uai_test_dwt.CYCCNT = 165U;
    monitor.RecordTaskLoop(2, loop_start);
    uai_test_dwt.CYCCNT = 200U;
    monitor.Report();

    const auto *header = reinterpret_cast<const CpuTaskMonitorTraceHeader *>(trace_memory);
    const auto *records =
        reinterpret_cast<const CpuTaskMonitorTraceRecord *>(trace_memory + kCpuTaskMonitorTraceDataOffset);
    ASSERT_EQ(header->record_count, 4U);
    EXPECT_EQ(header->next_sequence, 4U);
    EXPECT_EQ(header->last_period_cycles, 100U);
    EXPECT_EQ(header->last_interrupt_percent, 10U);
    EXPECT_EQ(records[0].type, static_cast<std::uint8_t>(CpuTaskMonitorTraceRecordType::kTaskLoopInterval));
    EXPECT_EQ(records[0].cycles, 15U);
    EXPECT_EQ(records[1].type, static_cast<std::uint8_t>(CpuTaskMonitorTraceRecordType::kReport));
    EXPECT_EQ(records[1].commit_marker, kCpuTaskMonitorTraceCommitMagic ^ 1U);
    EXPECT_EQ(records[2].task_id, 2U);
    EXPECT_EQ(records[2].cycles, 80U);
    EXPECT_EQ(records[2].dispatch_count, 1U);
    EXPECT_EQ(records[3].type, static_cast<std::uint8_t>(CpuTaskMonitorTraceRecordType::kTaskLoop));
    EXPECT_EQ(records[3].cycles, 15U);
    EXPECT_TRUE(monitor.Stop().Ok());
    EXPECT_EQ(dispatch_hook.exec, nullptr);
    EXPECT_EQ(interrupt_hook.enter, nullptr);
    EXPECT_TRUE(WriteDumpIfRequested());
}

} // namespace
} // namespace uai::ai::middleware::cpu_task_monitor