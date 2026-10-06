#pragma once

#include <cstdint>

#include "common/error.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "memory_allocator/memory_allocator.hpp"

namespace uai::ai::camera {

struct Diagnostics {
    std::uint32_t vsync_event_count = 0U;
    std::uint32_t frame_event_count = 0U;
    std::uint32_t recovery_count = 0U;
    std::uint32_t recovery_error_count = 0U;
    std::uint32_t isp_error_count = 0U;
    std::uint32_t dcmipp_last_status = 0U;
    std::uint32_t dcmipp_error_count = 0U;
    std::uint32_t camera_error_count = 0U;
    std::uint32_t pipe2_frame_event_count = 0U;
    std::uint32_t pipe2_drop_count = 0U;
    /* Sequence of every Pipe2 completion, including dropped frames. */
    std::uint32_t pipe2_latest_capture_sequence = 0U;
    std::uint32_t csi_last_status = 0U;
    std::uint32_t csi_last_status1 = 0U;
    std::uint32_t csi_last_pending_status = 0U;
    std::uint32_t csi_last_pending_status1 = 0U;
    std::uint32_t csi_error_count = 0U;
    std::uint32_t csi_last_error_code = 0U;
    std::uint32_t csi_sot_sync_dl0_count = 0U;
    std::uint32_t csi_sot_sync_dl1_count = 0U;
    std::uint32_t csi_sot_dl0_count = 0U;
    std::uint32_t csi_sot_dl1_count = 0U;
};

/* Application-facing capture driver. It owns camera state, buffer ownership,
 * and the backend lifecycle; sensor register details stay in registers/. */
class CameraDriver final {
public:
    common::Error Initialize(memory_allocator::MemoryAllocator &memory,
                             cache::CacheDriver &cache);
    void KeepClocksOnSleep() const;
    common::Error Start();
    common::Error Stop();
    common::Error Process();
    common::Error TakeCompletedCapture(memory_allocator::CaptureFrame *frame);
    common::Error TakeCompletedInference(memory_allocator::InferenceFrame *frame);
    common::Error SnapshotInferenceSource(memory_allocator::InferenceFrame *frame);
    Diagnostics GetDiagnostics() const;

private:
    memory_allocator::MemoryAllocator *memory_ = nullptr;
    cache::CacheDriver *cache_ = nullptr;
    bool initialized_ = false;
    bool started_ = false;
};

} // namespace uai::ai::camera
