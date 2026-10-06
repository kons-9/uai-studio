#pragma once

namespace uai::ai::psram::registers {

/* PSRAM controller/BSP access. Buffer policy belongs to the allocator. */
class PsramRegisterLayer final {
public:
    bool Initialize();
};

} // namespace uai::ai::psram::registers
