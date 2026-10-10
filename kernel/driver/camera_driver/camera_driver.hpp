#pragma once

#include <cstdint>

#include "middleware/foundation/error.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "driver/driver_ownership.hpp"
#include "middleware/pipeline/frame_types.hpp"
#include "memory_manager/memory_manager.hpp"
#include "driver/camera_driver/capture_configuration.hpp"

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
    common::Error Recover(const Writer &writer);
    common::Error Configure(
        const Geometry &geometry,
        const Writer &writer
    );
    common::Error GetGeometry(
        Geometry *geometry,
        const Writer &writer
    ) const;
    common::Error ReadState(
        State *state,
        const Writer &writer
    ) const;
    common::Error ApplyState(
        const State &state,
        const Writer &writer
    );
    common::Error AutoExposure(
        bool enabled,
        const Writer &writer
    );
    common::Error Compensation(
        int half_stops,
        const Writer &writer
    );
    common::Error Manual(
        std::int32_t exposure_us,
        std::int32_t gain_mdB,
        const Writer &writer
    );
    common::Error Statistics(
        Rect rectangle,
        const Writer &writer
    );
    common::Error WhiteBalance(
        std::uint32_t temperature,
        const Writer &writer
    );
    common::Error ListWhiteBalance(
        std::uint32_t *temperatures,
        std::size_t capacity,
        std::size_t *count,
        const Writer &writer
    ) const;
    bool Running(const Writer &writer) const { return management_->Validate(writer).Ok() && started_; }

private:
    friend class CameraManagement;
    ~CameraDriver() = default;
    CameraDriver(driver::ResourceManagement &management) : management_(&management) {}
    common::Error Initialize(
        memory_manager::MemoryManager &memory,
        cache::CacheManagement &cache
    );
    common::Error Initialize(const CaptureConfiguration &configuration);
    common::Error InitializeExternalHardware();
    common::Error StartExternal(const Writer &writer);
    common::Error ValidateControl(const Writer &writer) const;
    driver::ResourceManagement *management_;
    memory_manager::MemoryManager *memory_ = nullptr;
    cache::CacheManagement *cache_ = nullptr;
    bool initialized_ = false;
    bool started_ = false;
    bool external_capture_ = false;
    CaptureConfiguration capture_{};
    Rect pipe1_crop_{};
    mutable State saved_controls_{};
    mutable bool have_saved_controls_ = false;
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
    common::Error Initialize(const CaptureConfiguration &configuration) { return driver_.Initialize(configuration); }
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
    common::Error Recover()
    {
        return WithWriter([](CameraDriver &driver, const Writer &writer) {
            return driver.Recover(writer);
        });
    }
    common::Error Configure(const Geometry &geometry)
    {
        return WithWriter([&](CameraDriver &driver, const Writer &writer) {
            return driver.Configure(geometry, writer);
        });
    }
    common::Error GetGeometry(Geometry *geometry)
    {
        return WithWriter([&](CameraDriver &driver, const Writer &writer) {
            return driver.GetGeometry(geometry, writer);
        });
    }
    common::Error ReadState(State *state)
    {
        return WithWriter([&](CameraDriver &driver, const Writer &writer) {
            return driver.ReadState(state, writer);
        });
    }
    common::Error ApplyState(const State &state)
    {
        return WithWriter([&](CameraDriver &driver, const Writer &writer) {
            return driver.ApplyState(state, writer);
        });
    }
    common::Error AutoExposure(bool enabled)
    {
        return WithWriter([&](CameraDriver &driver, const Writer &writer) {
            return driver.AutoExposure(enabled, writer);
        });
    }
    common::Error Compensation(int half_stops)
    {
        return WithWriter([&](CameraDriver &driver, const Writer &writer) {
            return driver.Compensation(half_stops, writer);
        });
    }
    common::Error Manual(
        std::int32_t exposure_us,
        std::int32_t gain_mdB
    )
    {
        return WithWriter([&](CameraDriver &driver, const Writer &writer) {
            return driver.Manual(exposure_us, gain_mdB, writer);
        });
    }
    common::Error Statistics(Rect rectangle)
    {
        return WithWriter([&](CameraDriver &driver, const Writer &writer) {
            return driver.Statistics(rectangle, writer);
        });
    }
    common::Error WhiteBalance(std::uint32_t temperature)
    {
        return WithWriter([&](CameraDriver &driver, const Writer &writer) {
            return driver.WhiteBalance(temperature, writer);
        });
    }
    common::Error ListWhiteBalance(
        std::uint32_t *temperatures,
        std::size_t capacity,
        std::size_t *count
    )
    {
        return WithWriter([&](CameraDriver &driver, const Writer &writer) {
            return driver.ListWhiteBalance(temperatures, capacity, count, writer);
        });
    }
    bool Running()
    {
        Accessor accessor;
        return Acquire(&accessor).Ok() && accessor->Running(accessor.Ownership());
    }
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
