#ifndef UAI_AI_PSRAM_REGISTERS_HPP
#define UAI_AI_PSRAM_REGISTERS_HPP

namespace uai::ai::psram::registers {

/* PSRAM controller/BSP access. Buffer policy belongs to the allocator. */
class PsramRegisterLayer final {
public:
    bool Initialize();
};

} // namespace uai::ai::psram::registers

#endif // UAI_AI_PSRAM_REGISTERS_HPP
