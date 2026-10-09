#pragma once

#include <cstdint>

namespace uai::ai::lcd {

struct DisplayState {
    bool enabled = false;
    bool layer_enabled = false;
    bool rgb565 = false;
    std::uintptr_t framebuffer = 0;
    std::uint32_t lines = 0;
};

DisplayState ReadDisplayState();

}