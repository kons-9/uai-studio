#pragma once

#include <cstddef>
#include <cstdint>

/* On-PSRAM layout read by host_app/ai_model_monitor. No OS or HAL types;
 * changing any field, size, or offset requires a version bump and a decoder
 * update. */
namespace uai::ai::middleware::ai_model_monitor {

/* The trace is intentionally a raw fixed-layout format. It is stored in the
 * linker-reserved PSRAM trace region and can be copied with SWD after a halt.
 * PSRAM avoids taking the model NOR out of memory-mapped mode while the NPU
 * may still be reading weights. */
inline constexpr std::uint32_t kThreadMonitorTraceMagic = 0x544D4F4EU;
inline constexpr std::uint16_t kThreadMonitorTraceVersion = 5U;
inline constexpr std::uint32_t kThreadMonitorTraceCommitMagic = 0x434D4954U;
inline constexpr std::uint32_t kUnknownModelKindId = 0xFFFFFFFFU;
inline constexpr std::size_t kThreadMonitorModelNameCapacity = 16U;
inline constexpr std::size_t kThreadMonitorModelNameBytes = 28U;

struct ThreadMonitorTraceModelName {
    std::uint32_t model_kind_id = kUnknownModelKindId;
    char name[kThreadMonitorModelNameBytes] = {};
};

static_assert(sizeof(ThreadMonitorTraceModelName) == 32U);
inline constexpr std::uint16_t kThreadMonitorTraceDataOffset =
    static_cast<std::uint16_t>(
        64U + kThreadMonitorModelNameCapacity *
                  sizeof(ThreadMonitorTraceModelName));

enum class TraceRecordType : std::uint8_t {
    kSample = 1U,
    kFault = 2U,
    kAiRuntimeStep = 7U,
};

enum class TraceFaultCode : std::uint32_t {
    kNone = 0U,
    kTaskReference = 1U,
    kOperationTimeout = 2U,
};

inline constexpr std::uint8_t kTraceFlagOperationActive = 1U << 0U;
inline constexpr std::uint8_t kTraceFlagFaulted = 1U << 1U;
inline constexpr std::uint8_t kTraceFlagTimingValid = 1U << 2U;
/* For kAiRuntimeStep records, absence of this bit means the end record. */
inline constexpr std::uint8_t kTraceFlagAiRuntimeBegin = 1U << 4U;

struct alignas(32) ThreadMonitorTraceHeader {
    std::uint32_t magic = kThreadMonitorTraceMagic;
    std::uint16_t version = kThreadMonitorTraceVersion;
    std::uint16_t header_size = kThreadMonitorTraceDataOffset;
    std::uint32_t record_size = 0U;
    std::uint32_t capacity = 0U;
    std::uint32_t write_index = 0U;
    std::uint32_t record_count = 0U;
    std::uint32_t dropped_count = 0U;
    std::uint32_t boot_count = 0U;
    std::uint32_t monitored_task_id = 0U;
    std::uint32_t monitor_task_id = 0U;
    std::uint32_t last_fault_code = 0U;
    std::uint32_t fault_count = 0U;
    std::uint32_t next_sequence = 0U;
    std::uint32_t model_name_count = 0U;
    std::uint32_t model_name_entry_size =
        sizeof(ThreadMonitorTraceModelName);
    std::uint32_t reserved = 0U;
};

struct alignas(32) ThreadMonitorTraceRecord {
    std::uint32_t sequence = 0U;
    std::uint32_t timestamp_ms = 0U;
    std::uint32_t monitored_task_id = 0U;
    std::uint32_t monitor_task_id = 0U;
    std::uint32_t task_state = 0U;
    std::uint32_t wait_factor = 0U;
    std::uint32_t wait_object_id = 0U;
    std::int32_t current_priority = 0;
    std::int32_t base_priority = 0;
    std::uint32_t progress_tick = 0U;
    std::int32_t reference_status = 0;
    std::uint32_t fault_code = 0U;
    std::uint8_t type = static_cast<std::uint8_t>(TraceRecordType::kSample);
    std::uint8_t flags = 0U;
    /* ai_runtime step ID for kAiRuntimeStep records. */
    std::uint16_t phase_id = 0U;
    std::uint32_t commit_marker = 0U;
    /* Step duration for an end record; zero for a begin record. */
    std::uint32_t npu_elapsed_ms = 0U;
    std::uint32_t model_kind_id = kUnknownModelKindId;
};

static_assert(sizeof(ThreadMonitorTraceHeader) == 64U);
static_assert(sizeof(ThreadMonitorTraceRecord) == 64U);

} // namespace uai::ai::middleware::ai_model_monitor
