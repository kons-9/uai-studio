#pragma once

#include <cstdint>

#include "middleware/foundation/error.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "driver/driver_ownership.hpp"
#include "middleware/pipeline/frame_types.hpp"
#include "memory_manager/memory_manager.hpp"

namespace uai::ai::camera {

class CameraManagement;

struct Diagnostics {
    Diagnostics();

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

    common::Error AcquireWriter(
        Writer *writer,
        TMO timeout = TMO_FEVR
    ) const
    {
        return management_->Acquire(writer, timeout);
    }
    void KeepClocksOnSleep() const;
    void KeepClocksOnSleep(const Writer &writer) const;
    common::Error Start();
    common::Error Start(const Writer &writer);
    common::Error Stop();
    common::Error Stop(const Writer &writer);
    common::Error Process();
    common::Error Process(const Writer &writer);
    common::Error TakeCompletedCapture(pipeline::CaptureFrame *frame);
    common::Error TakeCompletedCapture(
        pipeline::CaptureFrame *frame,
        const Writer &writer
    );
    common::Error TakeCompletedInference(pipeline::InferenceFrame *frame);
    common::Error TakeCompletedInference(
        pipeline::InferenceFrame *frame,
        const Writer &writer
    );
    common::Error SnapshotInferenceSource(pipeline::InferenceFrame *frame);
    common::Error SnapshotInferenceSource(
        pipeline::InferenceFrame *frame,
        const Writer &writer
    );
    Diagnostics GetDiagnostics() const;

private:
    friend class CameraManagement;
    ~CameraDriver() = default;
    CameraDriver(driver::ResourceManagement &management) : management_(&management) {}
    common::Error Initialize(
        memory_manager::MemoryManager &memory,
        cache::CacheManagement &cache
    );
    driver::ResourceManagement *management_;
    memory_manager::MemoryManager *memory_ = nullptr;
    cache::CacheManagement *cache_ = nullptr;
    bool initialized_ = false;
    bool started_ = false;
};

class CameraManagement final {
public:
    using Writer = CameraDriver::Writer;
    using Accessor = driver::ResourceAccessor<CameraDriver>;
    static CameraManagement &Instance()
    {
        static CameraManagement m;
        return m;
    }
    common::Error Initialize(
        memory_manager::MemoryManager &memory,
        cache::CacheManagement &cache
    )
    {
        return driver_.Initialize(memory, cache);
    }
    common::Error Acquire(
        Accessor *a,
        TMO timeout = TMO_FEVR
    )
    {
        if (!a)
            return {common::ErrorCode::kInvalidArgument};
        *a = {};
        Writer w;
        auto s = ownership_.Acquire(&w, timeout);
        if (s.Ok()) {
            *a = Accessor(driver_, static_cast<Writer &&>(w));
        }
        return s;
    }
    common::Error Validate(const Writer &w) const { return ownership_.Validate(w); }
    common::Error Start()
    {
        return WithWriter([](CameraDriver &d, const Writer &w) {
            return d.Start(w);
        });
    }
    common::Error Stop()
    {
        return WithWriter([](CameraDriver &d, const Writer &w) {
            return d.Stop(w);
        });
    }
    common::Error Process()
    {
        return WithWriter([](CameraDriver &d, const Writer &w) {
            return d.Process(w);
        });
    }
    common::Error TakeCompletedCapture(pipeline::CaptureFrame *f)
    {
        return WithWriter([&](CameraDriver &d, const Writer &w) {
            return d.TakeCompletedCapture(f, w);
        });
    }
    common::Error TakeCompletedInference(pipeline::InferenceFrame *f)
    {
        return WithWriter([&](CameraDriver &d, const Writer &w) {
            return d.TakeCompletedInference(f, w);
        });
    }
    common::Error SnapshotInferenceSource(pipeline::InferenceFrame *f)
    {
        return WithWriter([&](CameraDriver &d, const Writer &w) {
            return d.SnapshotInferenceSource(f, w);
        });
    }
    void KeepClocksOnSleep()
    {
        (void)WithWriter([](CameraDriver &d, const Writer &w) {
            d.KeepClocksOnSleep(w);
            return common::Error{};
        });
    }
    Diagnostics GetDiagnostics() const { return driver_.GetDiagnostics(); }
    CameraManagement(const CameraManagement &) = delete;
    CameraManagement &operator=(const CameraManagement &) = delete;

private:
    CameraManagement() : driver_(ownership_) {}
    ~CameraManagement() = default;
    template <typename F>
    common::Error WithWriter(F f)
    {
        Accessor a;
        auto s = Acquire(&a);
        return s.Ok() ? f(*a.Get(), a.Ownership()) : s;
    }
    driver::ResourceManagement ownership_{};
    CameraDriver driver_;
};

} // namespace uai::ai::camera
