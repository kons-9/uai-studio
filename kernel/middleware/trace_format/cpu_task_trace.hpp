#pragma once

#include <cstddef>
#include <cstdint>

/* On-PSRAM layout read by host_app/cpu_task_monitor. No OS or HAL types;
 * changing any field, size, or offset requires a version bump and a decoder
 * update. */
namespace uai::ai::middleware::cpu_task_monitor {

inline constexpr std::uint32_t kCpuTaskMonitorTraceMagic = 0x43544D4EU;
inline constexpr std::uint16_t kCpuTaskMonitorTraceVersion = 3U;
inline constexpr std::uint32_t kCpuTaskMonitorTraceCommitMagic = 0x43544D43U;
inline constexpr std::size_t kCpuTaskMonitorTaskNameCount = 33U;
inline constexpr std::size_t kCpuTaskMonitorTaskNameBytes = 28U;

struct CpuTaskMonitorTraceTaskName {
    std::uint32_t task_id = 0U;
    char name[kCpuTaskMonitorTaskNameBytes] = {};
};

static_assert(sizeof(CpuTaskMonitorTraceTaskName) == 32U);
inline constexpr std::uint32_t kCpuTaskMonitorTraceDataOffset =
    64U + kCpuTaskMonitorTaskNameCount * sizeof(CpuTaskMonitorTraceTaskName);

enum class CpuTaskMonitorTraceRecordType : std::uint8_t {
    kReport = 1U,
    kTask = 2U,
    kTaskLoop = 3U,
    kTaskLoopInterval = 4U,
};

struct alignas(32) CpuTaskMonitorTraceHeader {
    std::uint32_t magic = kCpuTaskMonitorTraceMagic;
    std::uint16_t version = kCpuTaskMonitorTraceVersion;
    std::uint16_t header_size = kCpuTaskMonitorTraceDataOffset;
    std::uint32_t record_size = 0U;
    std::uint32_t capacity = 0U;
    std::uint32_t write_index = 0U;
    std::uint32_t record_count = 0U;
    std::uint32_t dropped_count = 0U;
    std::uint32_t boot_count = 0U;
    std::uint32_t next_sequence = 0U;
    std::uint32_t monitor_task_id = 0U;
    std::uint32_t report_task_id = 0U;
    std::uint32_t last_period_cycles = 0U;
    std::uint32_t last_interrupt_percent = 0U;
    std::uint32_t task_name_count = 0U;
    std::uint32_t task_name_entry_size = sizeof(CpuTaskMonitorTraceTaskName);
    std::uint32_t reserved = 0U;
};

struct alignas(32) CpuTaskMonitorTraceRecord {
    std::uint32_t sequence = 0U;
    std::uint32_t timestamp_cycles = 0U;
    std::uint32_t period_cycles = 0U;
    std::uint32_t task_id = 0U;
    std::uint32_t state = 0U;
    std::uint32_t cycles = 0U;
    std::uint32_t dispatch_count = 0U;
    std::uint32_t usage_percent = 0U;
    std::uint32_t interrupt_percent = 0U;
    std::uint32_t interrupt_count = 0U;
    std::uint32_t unknown_task_events = 0U;
    std::uint8_t type = static_cast<std::uint8_t>(CpuTaskMonitorTraceRecordType::kTask);
    std::uint8_t reserved0[3] = {};
    std::uint32_t commit_marker = 0U;
    std::uint32_t reserved1[3] = {};
};

static_assert(sizeof(CpuTaskMonitorTraceHeader) == 64U);
static_assert(sizeof(CpuTaskMonitorTraceRecord) == 64U);

} // namespace uai::ai::middleware::cpu_task_monitor
