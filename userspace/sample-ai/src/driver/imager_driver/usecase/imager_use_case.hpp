#ifndef UAI_AI_IMAGER_USE_CASE_HPP
#define UAI_AI_IMAGER_USE_CASE_HPP

#include <cstdint>

#include "common/error.hpp"
#include "memory_manager/memory_hardware.hpp"
#include "memory_manager/memory_manager.hpp"

namespace uai::ai::imager::usecase {

/* Application-facing capture use case. It owns camera state, buffer ownership,
 * and the backend lifecycle; sensor register details stay in registers/. */
class ImagerUseCase final {
public:
    common::Error Initialize(memory_manager::MemoryManager &memory,
                             memory_manager::MemoryHardware &memory_hardware);
    common::Error Start();
    common::Error Stop();
    common::Error Process();
    common::Error TakeCompletedCapture(memory_manager::CaptureFrame *frame);

private:
    memory_manager::MemoryManager *memory_ = nullptr;
    memory_manager::MemoryHardware *memory_hardware_ = nullptr;
    bool initialized_ = false;
    bool started_ = false;
};

} // namespace uai::ai::imager::usecase

#endif // UAI_AI_IMAGER_USE_CASE_HPP
