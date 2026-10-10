#pragma once

#include "tests/framework.hpp"
#include "ui/console.hpp"
#include "middleware/ui/touch_point.hpp"

namespace uai::hwtest::integrated {

struct Observation {
    std::uint32_t pipe1, pipe2, errors, recoveries;
    bool running;
};

void Initialize(const console::Writer &writer);
void Service();
void DisplayFailure();
Observation Observe();
uai::ai::ui::TouchPoint Touch();
Result CameraPipes(const Context &context);
Result CameraControl(const Context &context);
Result TouchRead(const Context &context);
Result TouchInteractive(const Context &context);
Result Dma2dSuite(const Context &context);
console::Status Control(
    void *,
    int,
    const char *const *,
    const console::Writer &
);
console::Status Capture(
    void *,
    int,
    const char *const *,
    const console::Writer &
);

}