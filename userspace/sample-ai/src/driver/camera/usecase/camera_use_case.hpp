#ifndef UAI_AI_CAMERA_USE_CASE_HPP
#define UAI_AI_CAMERA_USE_CASE_HPP

#include <cstdint>

#include "common/error.hpp"
#include "driver/camera/registers/imx335_registers.hpp"
#include "memory_manager/memory_hardware.hpp"
#include "memory_manager/memory_manager.hpp"

namespace uai::ai::camera::usecase {

/* Application-facing capture use case. It owns camera state, buffer ownership,
 * and the backend lifecycle; sensor register details stay in registers/. */
class CameraUseCase final {
public:
    common::Error Initialize(memory_manager::MemoryManager &memory,
                             memory_manager::MemoryHardware &memory_hardware);
    common::Error Start();
    common::Error Stop();
    common::Error Process();
    common::Error TakeCompletedCapture(memory_manager::CaptureFrame *frame);

    common::Error ReadSensorRegisters(
        registers::SensorRegisterSnapshot *snapshot) const;

private:
    memory_manager::MemoryManager *memory_ = nullptr;
    memory_manager::MemoryHardware *memory_hardware_ = nullptr;
    registers::Imx335RegisterLayer registers_{};
    bool initialized_ = false;
    bool started_ = false;
};

} // namespace uai::ai::camera::usecase

#endif // UAI_AI_CAMERA_USE_CASE_HPP
