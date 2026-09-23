#ifndef UAI_SAMPLE2_CAMERA_DRIVER_HPP
#define UAI_SAMPLE2_CAMERA_DRIVER_HPP

#include "common/error.hpp"
#include "memory_manager/memory_manager.hpp"

#include "driver/camera_driver.hpp"

namespace uai::sample2 {

class CameraDriver final {
public:
    common::Error Initialize(memory_manager::MemoryManager &memory);
    common::Error Start();
    common::Error Process();
    common::Error TakeCompletedCapture(
        memory_manager::CaptureFrame *frame);

private:
    memory_manager::MemoryManager *memory_ = nullptr;
    uai::driver::CameraDriver backend_{};
    bool initialized_ = false;
    bool started_ = false;
};

} // namespace uai::sample2

#endif
