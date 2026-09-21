#ifndef PERSON_DETECTOR_HPP
#define PERSON_DETECTOR_HPP

#include <cstdint>

#include "memory_manager.hpp"
#include "model_api.h"
#include "postprocess.hpp"

namespace person
{

    class Detector final
    {
    public:
        bool Initialize(memory::Manager &memory);

        bool Infer(std::uintptr_t camera_frame, memory::Manager &memory,
                   inference::ObjectDetection *detections,
                   std::uint32_t capacity, std::uint32_t *count);

        stai_return_code LastError() const { return last_error_; }

    private:
        static void ConvertCameraToInput(std::uintptr_t camera_frame,
                                         stai_ptr model_input);

        const model_api &model_ = person_model;
        stai_network_info info_{};
        stai_ptr input_ = nullptr;
        stai_ptr outputs_[3]{};
        bool initialized_ = false;
        stai_return_code last_error_ = STAI_SUCCESS;
    };

} // namespace person

#endif /* PERSON_DETECTOR_HPP */
