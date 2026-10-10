#pragma once

#include "middleware/foundation/error.hpp"
#include "middleware/ui/touch_point.hpp"

namespace uai::ai::touch::registers {

class TouchRegisterLayer final {
public:
    common::Error Initialize();
    common::Error Read(ui::TouchPoint &sample);
};

}