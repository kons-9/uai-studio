#ifndef UAI_AI_CAMERA_DRIVER_HPP
#define UAI_AI_CAMERA_DRIVER_HPP

#include <cstdint>

#include "common/error.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "memory_allocator/memory_allocator.hpp"

namespace uai::ai::camera {

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

private:
    memory_allocator::MemoryAllocator *memory_ = nullptr;
    cache::CacheDriver *cache_ = nullptr;
    bool initialized_ = false;
    bool started_ = false;
};

} // namespace uai::ai::camera

#endif // UAI_AI_CAMERA_DRIVER_HPP
