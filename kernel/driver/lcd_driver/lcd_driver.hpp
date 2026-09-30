#ifndef UAI_AI_LCD_DRIVER_HPP
#define UAI_AI_LCD_DRIVER_HPP

#include <cstdint>

#include "common/error.hpp"
#include "driver/driver_status.hpp"
#include "driver/driver_ownership.hpp"
#include "driver/lcd_driver/registers/lcd_registers.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "middleware/pipeline/frame_types.hpp"
#include "memory_manager/memory_manager.hpp"
#include "middleware/ai_runtime/inference_result_types.hpp"

namespace uai::ai::lcd {

class LcdManagement;

class LcdDriver final {
public:
    using Writer = driver::ResourceManagement::Writer;

    common::Error AcquireWriter(Writer *writer, TMO timeout = TMO_FEVR) const
    { return management_->Acquire(writer, timeout); }
    void KeepClocksOnSleep() const;
    void KeepClocksOnSleep(const Writer &writer) const;
    common::Error ShowInitialFrame(
        const inference::BoxSet &boxes,
        bool coordinate_pattern = false);
    common::Error ShowInitialFrame(
        const inference::BoxSet &boxes, const Writer &writer,
        bool coordinate_pattern = false);
    common::Error SynchronizeCurrentFrame();
    common::Error SynchronizeCurrentFrame(const Writer &writer);
    common::Error GenerateCoordinatePattern(
        const memory_allocator::Buffer &destination) const;
    common::Error GenerateCoordinatePattern(
        const memory_allocator::Buffer &destination,
        const Writer &writer) const;
    common::Error ComposeAndPresent(
        const pipeline::CaptureFrame &capture,
        const inference::BoxSet &boxes,
        bool log_copy_crc = false);
    common::Error ComposeAndPresent(
        const pipeline::CaptureFrame &capture,
        const inference::BoxSet &boxes, const Writer &writer,
        bool log_copy_crc = false);
    common::Error ComposeInferenceAndPresent(
        const pipeline::InferenceFrame &frame);
    common::Error ComposeInferenceAndPresent(
        const pipeline::InferenceFrame &frame, const Writer &writer);
    void SetTimingDiagnostics(bool enabled, const Writer &writer);

private:
    friend class LcdManagement;
    ~LcdDriver() = default;
    LcdDriver(driver::ResourceManagement &management) : management_(&management) {}
    common::Error Initialize(memory_manager::MemoryManager &memory,
                             cache::CacheManagement &cache);
    driver::ResourceManagement *management_;
    static void FillInitialFrame(const pipeline::DisplayBuffer &buffer,
                                 const inference::BoxSet &boxes,
                                 bool coordinate_pattern);
    static void DrawBoxes(const pipeline::DisplayBuffer &buffer,
                          const inference::BoxSet &boxes);
    static void DrawInferenceRegion(
        const pipeline::DisplayBuffer &buffer);
    static void DrawMask(const pipeline::DisplayBuffer &buffer,
                         const inference::BoxSet &boxes);
    static common::Error FromBackend(uai::driver::DriverStatus status,
                                     const char *operation);
    memory_manager::MemoryManager *memory_ = nullptr;
    cache::CacheManagement *cache_ = nullptr;
    registers::LcdRegisterLayer registers_{};
    bool initialized_ = false;
    bool timing_diagnostics_ = false;
};

class LcdManagement final {
public:
    using Writer = LcdDriver::Writer;
    using Accessor = driver::ResourceAccessor<LcdDriver>;
    static LcdManagement &Instance() { static LcdManagement m; return m; }
    common::Error Initialize(memory_manager::MemoryManager &memory, cache::CacheManagement &cache)
    { return driver_.Initialize(memory, cache); }
    common::Error Acquire(Accessor *a, TMO timeout = TMO_FEVR)
    {
        if (!a) return {common::ErrorCode::kInvalidArgument, 0U, "lcd.management.acquire.null_accessor"};
        *a = {}; Writer w; auto s = ownership_.Acquire(&w, timeout);
        if (s.Ok()) *a = Accessor(driver_, static_cast<Writer &&>(w));
        return s;
    }
    common::Error Validate(const Writer &w, const char *op) const { return ownership_.Validate(w, op); }
    void KeepClocksOnSleep() { (void)WithWriter([](LcdDriver &d, const Writer &w) { d.KeepClocksOnSleep(w); return common::Error{}; }); }
    common::Error ShowInitialFrame(const inference::BoxSet &b, bool p = false) { return WithWriter([&](LcdDriver &d, const Writer &w) { return d.ShowInitialFrame(b, w, p); }); }
    common::Error SynchronizeCurrentFrame() { return WithWriter([](LcdDriver &d, const Writer &w) { return d.SynchronizeCurrentFrame(w); }); }
    common::Error GenerateCoordinatePattern(const memory_allocator::Buffer &b) { return WithWriter([&](LcdDriver &d, const Writer &w) { return d.GenerateCoordinatePattern(b, w); }); }
    common::Error ComposeAndPresent(const pipeline::CaptureFrame &f, const inference::BoxSet &b, bool crc = false) { return WithWriter([&](LcdDriver &d, const Writer &w) { return d.ComposeAndPresent(f, b, w, crc); }); }
    common::Error ComposeInferenceAndPresent(const pipeline::InferenceFrame &f) { return WithWriter([&](LcdDriver &d, const Writer &w) { return d.ComposeInferenceAndPresent(f, w); }); }
    void SetTimingDiagnostics(bool enabled) { (void)WithWriter([&](LcdDriver &d, const Writer &w) { d.SetTimingDiagnostics(enabled, w); return common::Error{}; }); }
    LcdManagement(const LcdManagement &) = delete;
    LcdManagement &operator=(const LcdManagement &) = delete;
private:
    LcdManagement() : driver_(ownership_) {} ~LcdManagement() = default;
    template <typename F> common::Error WithWriter(F f) { Accessor a; auto s = Acquire(&a); return s.Ok() ? f(*a.Get(), a.Ownership()) : s; }
    driver::ResourceManagement ownership_{}; LcdDriver driver_;
};

} // namespace uai::ai::lcd

#endif
