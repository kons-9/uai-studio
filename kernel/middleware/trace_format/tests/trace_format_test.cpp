#include "middleware/trace_format/ai_model_trace.hpp"
#include "middleware/trace_format/cpu_task_trace.hpp"

#include <cstddef>
#include <type_traits>

#include <gtest/gtest.h>

/* The host decoders unpack these layouts with fixed struct strings:
 *   ai_model_monitor:  header "<IHH14I", record "<12IBBH3I", name "<I28s"
 *   cpu_task_monitor:  header "<IHH14I", record "<11IB3BI3I", name "<I28s"
 * Every offset below is part of that contract. */

namespace uai::ai::middleware {
namespace {

using ai_model_monitor::ThreadMonitorTraceHeader;
using ai_model_monitor::ThreadMonitorTraceModelName;
using ai_model_monitor::ThreadMonitorTraceRecord;
using cpu_task_monitor::CpuTaskMonitorTraceHeader;
using cpu_task_monitor::CpuTaskMonitorTraceRecord;
using cpu_task_monitor::CpuTaskMonitorTraceTaskName;

TEST(TraceFormat, AiModelLayoutMatchesDecoder)
{
    static_assert(std::is_trivially_copyable_v<ThreadMonitorTraceHeader>);
    static_assert(std::is_trivially_copyable_v<ThreadMonitorTraceRecord>);
    static_assert(sizeof(ThreadMonitorTraceHeader) == 64U);
    static_assert(sizeof(ThreadMonitorTraceRecord) == 64U);
    static_assert(sizeof(ThreadMonitorTraceModelName) == 32U);
    static_assert(alignof(ThreadMonitorTraceRecord) == 32U);
    static_assert(ai_model_monitor::kThreadMonitorTraceVersion == 5U);
    static_assert(ai_model_monitor::kThreadMonitorTraceDataOffset ==
                  64U + 16U * 32U);

    EXPECT_EQ(offsetof(ThreadMonitorTraceHeader, magic), 0U);
    EXPECT_EQ(offsetof(ThreadMonitorTraceHeader, version), 4U);
    EXPECT_EQ(offsetof(ThreadMonitorTraceHeader, header_size), 6U);
    EXPECT_EQ(offsetof(ThreadMonitorTraceHeader, record_size), 8U);
    EXPECT_EQ(offsetof(ThreadMonitorTraceHeader, model_name_count), 52U);

    EXPECT_EQ(offsetof(ThreadMonitorTraceRecord, sequence), 0U);
    EXPECT_EQ(offsetof(ThreadMonitorTraceRecord, fault_code), 44U);
    EXPECT_EQ(offsetof(ThreadMonitorTraceRecord, type), 48U);
    EXPECT_EQ(offsetof(ThreadMonitorTraceRecord, flags), 49U);
    EXPECT_EQ(offsetof(ThreadMonitorTraceRecord, phase_id), 50U);
    EXPECT_EQ(offsetof(ThreadMonitorTraceRecord, commit_marker), 52U);
    EXPECT_EQ(offsetof(ThreadMonitorTraceRecord, npu_elapsed_ms), 56U);
    EXPECT_EQ(offsetof(ThreadMonitorTraceRecord, model_kind_id), 60U);
    EXPECT_EQ(offsetof(ThreadMonitorTraceModelName, name), 4U);

    ThreadMonitorTraceHeader header{};
    EXPECT_EQ(header.magic, ai_model_monitor::kThreadMonitorTraceMagic);
    EXPECT_EQ(header.header_size, ai_model_monitor::kThreadMonitorTraceDataOffset);
    EXPECT_EQ(header.model_name_entry_size, 32U);
}

TEST(TraceFormat, CpuTaskLayoutMatchesDecoder)
{
    static_assert(std::is_trivially_copyable_v<CpuTaskMonitorTraceHeader>);
    static_assert(std::is_trivially_copyable_v<CpuTaskMonitorTraceRecord>);
    static_assert(sizeof(CpuTaskMonitorTraceHeader) == 64U);
    static_assert(sizeof(CpuTaskMonitorTraceRecord) == 64U);
    static_assert(sizeof(CpuTaskMonitorTraceTaskName) == 32U);
    static_assert(cpu_task_monitor::kCpuTaskMonitorTraceVersion == 3U);
    static_assert(cpu_task_monitor::kCpuTaskMonitorTraceDataOffset ==
                  64U + 33U * 32U);

    EXPECT_EQ(offsetof(CpuTaskMonitorTraceHeader, version), 4U);
    EXPECT_EQ(offsetof(CpuTaskMonitorTraceHeader, task_name_count), 52U);
    EXPECT_EQ(offsetof(CpuTaskMonitorTraceRecord, unknown_task_events), 40U);
    EXPECT_EQ(offsetof(CpuTaskMonitorTraceRecord, type), 44U);
    EXPECT_EQ(offsetof(CpuTaskMonitorTraceRecord, commit_marker), 48U);
    EXPECT_EQ(offsetof(CpuTaskMonitorTraceTaskName, name), 4U);

    CpuTaskMonitorTraceHeader header{};
    EXPECT_EQ(header.magic, cpu_task_monitor::kCpuTaskMonitorTraceMagic);
    EXPECT_EQ(header.header_size, cpu_task_monitor::kCpuTaskMonitorTraceDataOffset);
}

} // namespace
} // namespace uai::ai::middleware
