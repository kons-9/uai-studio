#include "ui/display.hpp"
#include "tests/framework.hpp"
#include "driver/lcd_driver/display_state.hpp"

#include <cstdio>

namespace uai::hwtest::tests::display_driver {

Result Run(const Context &context)
{
    if (!display_log::SelfTest())
        return {Outcome::kFail, "display-initialization-or-framebuffer"};
    const auto state = uai::ai::lcd::ReadDisplayState();
    char line[160];
    std::snprintf(
        line,
        sizeof(line),
        "TRACE display enabled=%u layer=%u rgb565=%u framebuffer=%08lx lines=%lu",
        static_cast<unsigned>(state.enabled),
        static_cast<unsigned>(state.layer_enabled),
        static_cast<unsigned>(state.rgb565),
        static_cast<unsigned long>(state.framebuffer),
        static_cast<unsigned long>(state.lines)
    );
    context.Trace(line);
    if (!state.enabled || !state.layer_enabled || !state.rgb565 || !display_log::IsFramebuffer(state.framebuffer)
        || state.lines != 480) {
        return {Outcome::kFail, "display-scanout-configuration"};
    }
    return {Outcome::kPass, "ltdc-enabled-rgb565-framebuffer"};
}

}