#pragma once
#include "runtime.hpp"

namespace experiment {

struct Services {
    camera::Runtime *camera;
    console::Writer output;
    std::uint32_t (*clock)();
    void (*wait)(std::uint32_t);
    bool *controls_locked = nullptr;
};

std::size_t Register(
    const Services &services,
    console::Command *commands,
    std::size_t capacity
);
void Tick(
    std::uint32_t milliseconds,
    std::uint32_t sequence
);
}
