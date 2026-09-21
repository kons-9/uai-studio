#ifndef POSTPROCESS_HPP
#define POSTPROCESS_HPP

#include <cstdint>

#include "stai.h"

namespace inference {

enum class Mode : std::uint32_t {
    Person = 0,
};

struct ObjectDetection {
    float x_center;
    float y_center;
    float width;
    float height;
    float confidence;
};

bool initialize(Mode mode, const stai_network_info &info);
bool process_person(const stai_network_info &info, stai_ptr *outputs,
                    ObjectDetection *detections, std::uint32_t capacity,
                    std::uint32_t *count);

} // namespace inference

#endif /* POSTPROCESS_HPP */
