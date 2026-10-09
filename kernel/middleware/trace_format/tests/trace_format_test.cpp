#include "middleware/trace_format/ai_model_trace.hpp"
#include "middleware/trace_format/cpu_task_trace.hpp"
#include "middleware/trace_format/trace_ring.hpp"

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

TEST(
    TraceFormat,
    RingWriterWrapsAndCommitsBothTraceFormats
)
{
    ThreadMonitorTraceHeader ai_header{};
    ai_header.capacity = 1U;
    ThreadMonitorTraceRecord ai_records[1]{};
    unsigned int ai_flushes = 0U;
    const auto flush = [&ai_flushes](const void *, std::size_t) {
        ++ai_flushes;
    };
    EXPECT_TRUE(
        trace_format::AppendTraceRecord(
            &ai_header,
            ai_records,
            1U,
            ThreadMonitorTraceRecord{},
            ai_model_monitor::kThreadMonitorTraceCommitMagic,
            flush,
            [](ThreadMonitorTraceHeader &, const ThreadMonitorTraceRecord &) {}
        )
    );
    EXPECT_TRUE(
        trace_format::AppendTraceRecord(
            &ai_header,
            ai_records,
            1U,
            ThreadMonitorTraceRecord{},
            ai_model_monitor::kThreadMonitorTraceCommitMagic,
            flush,
            [](ThreadMonitorTraceHeader &, const ThreadMonitorTraceRecord &) {}
        )
    );
    EXPECT_EQ(ai_records[0].sequence, 1U);
    EXPECT_EQ(ai_records[0].commit_marker, ai_model_monitor::kThreadMonitorTraceCommitMagic ^ 1U);
    EXPECT_EQ(ai_header.record_count, 1U);
    EXPECT_EQ(ai_header.dropped_count, 1U);
    EXPECT_EQ(ai_header.next_sequence, 2U);
    EXPECT_EQ(ai_header.write_index, 0U);
    EXPECT_EQ(ai_flushes, 4U);

    CpuTaskMonitorTraceHeader cpu_header{};
    cpu_header.capacity = 1U;
    CpuTaskMonitorTraceRecord cpu_records[1]{};
    EXPECT_TRUE(
        trace_format::AppendTraceRecord(
            &cpu_header,
            cpu_records,
            1U,
            CpuTaskMonitorTraceRecord{},
            cpu_task_monitor::kCpuTaskMonitorTraceCommitMagic,
            [](const void *, std::size_t) {},
            [](CpuTaskMonitorTraceHeader &, const CpuTaskMonitorTraceRecord &) {}
        )
    );
    EXPECT_EQ(cpu_records[0].commit_marker, cpu_task_monitor::kCpuTaskMonitorTraceCommitMagic);
}

TEST(
    TraceFormat,
    AiModelLayoutMatchesDecoder
)
{
    static_assert(std::is_trivially_copyable_v<ThreadMonitorTraceHeader>);
    static_assert(std::is_trivially_copyable_v<ThreadMonitorTraceRecord>);
    static_assert(sizeof(ThreadMonitorTraceHeader) == 64U);
    static_assert(sizeof(ThreadMonitorTraceRecord) == 64U);
    static_assert(sizeof(ThreadMonitorTraceModelName) == 32U);
    static_assert(alignof(ThreadMonitorTraceRecord) == 32U);
    static_assert(ai_model_monitor::kThreadMonitorTraceVersion == 5U);
    static_assert(ai_model_monitor::kThreadMonitorTraceDataOffset == 64U + 16U * 32U);

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

TEST(
    TraceFormat,
    CpuTaskLayoutMatchesDecoder
)
{
    static_assert(std::is_trivially_copyable_v<CpuTaskMonitorTraceHeader>);
    static_assert(std::is_trivially_copyable_v<CpuTaskMonitorTraceRecord>);
    static_assert(sizeof(CpuTaskMonitorTraceHeader) == 64U);
    static_assert(sizeof(CpuTaskMonitorTraceRecord) == 64U);
    static_assert(sizeof(CpuTaskMonitorTraceTaskName) == 32U);
    static_assert(cpu_task_monitor::kCpuTaskMonitorTraceVersion == 3U);
    static_assert(cpu_task_monitor::kCpuTaskMonitorTraceDataOffset == 64U + 33U * 32U);

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
