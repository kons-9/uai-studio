#ifndef UAI_SAMPLE2_IMAGER_DRIVER_HPP
#define UAI_SAMPLE2_IMAGER_DRIVER_HPP

#include "common/error.hpp"
#include "memory_manager/memory_hardware.hpp"
#include "memory_manager/memory_manager.hpp"

#include "driver/camera_driver.hpp"

namespace uai::sample2 {

class ImagerDriver final {
public:
    common::Error Initialize(memory_manager::MemoryManager &memory,
                             memory_manager::MemoryHardware &memory_hardware);
    common::Error Start();
    common::Error Stop();
    common::Error Process();
    common::Error TakeCompletedCapture(
        memory_manager::CaptureFrame *frame);

private:
    memory_manager::MemoryManager *memory_ = nullptr;
    memory_manager::MemoryHardware *memory_hardware_ = nullptr;
    uai::driver::CameraDriver backend_{};
    bool initialized_ = false;
    bool started_ = false;
};

} // namespace uai::sample2

#endif
