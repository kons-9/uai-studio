#pragma once

#include <cstdint>

#include "driver/driver_status.hpp"

namespace uai::ai::lcd::registers {

/* LCD peripheral/register access. Frame composition stays in LcdDriver. */
class LcdRegisterLayer final {
public:
    uai::driver::DriverStatus Initialize(std::uintptr_t initial_buffer);
    uai::driver::DriverStatus Synchronize();
    uai::driver::DriverStatus Present(std::uintptr_t buffer);

private:
    bool initialized_ = false;
    bool reload_pending_ = false;
};

} // namespace uai::ai::lcd::registers
