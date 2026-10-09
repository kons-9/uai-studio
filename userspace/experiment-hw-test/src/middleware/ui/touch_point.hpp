#pragma once

#include <cstdint>

namespace uai::ai::ui {

/* One touch sample in screen pixels. Produced by the touch driver, consumed
 * by widgets; kept in its own header so drivers do not depend on widgets. */
struct TouchPoint {
    bool active = false;
    std::uint16_t x = 0U;
    std::uint16_t y = 0U;
};

} // namespace uai::ai::ui
