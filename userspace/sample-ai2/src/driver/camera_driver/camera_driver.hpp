#ifndef UAI_AI_CAMERA_DRIVER_HPP
#define UAI_AI_CAMERA_DRIVER_HPP

#include <cstdint>

#include "common/error.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "driver/driver_ownership.hpp"
#include "application/pipeline/frame_types.hpp"
#include "memory_manager/memory_manager.hpp"

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
    using Writer = driver::ResourceManagement::Writer;

    common::Error Initialize(memory_manager::MemoryManager &memory,
                             cache::CacheDriver &cache);
    common::Error AcquireWriter(Writer *writer, TMO timeout = TMO_FEVR) const
    { return management_.Acquire(writer, timeout); }
    void KeepClocksOnSleep() const;
    void KeepClocksOnSleep(const Writer &writer) const;
    common::Error Start();
    common::Error Start(const Writer &writer);
    common::Error Stop();
    common::Error Stop(const Writer &writer);
    common::Error Process();
    common::Error Process(const Writer &writer);
    common::Error TakeCompletedCapture(pipeline::CaptureFrame *frame);
    common::Error TakeCompletedCapture(pipeline::CaptureFrame *frame,
                                       const Writer &writer);
    common::Error TakeCompletedInference(pipeline::InferenceFrame *frame);
    common::Error TakeCompletedInference(pipeline::InferenceFrame *frame,
                                         const Writer &writer);
    common::Error SnapshotInferenceSource(pipeline::InferenceFrame *frame);
    common::Error SnapshotInferenceSource(pipeline::InferenceFrame *frame,
                                          const Writer &writer);
    Diagnostics GetDiagnostics() const;

private:
    driver::ResourceManagement management_{};
    memory_manager::MemoryManager *memory_ = nullptr;
    cache::CacheDriver *cache_ = nullptr;
    bool initialized_ = false;
    bool started_ = false;
};

} // namespace uai::ai::camera

#endif // UAI_AI_CAMERA_DRIVER_HPP
