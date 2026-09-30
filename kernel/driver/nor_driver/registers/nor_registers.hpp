#ifndef UAI_AI_NOR_REGISTERS_HPP
#define UAI_AI_NOR_REGISTERS_HPP

#include <cstddef>
#include <cstdint>

namespace uai::ai::nor::registers {

/* NOR controller/BSP access. Model policy belongs to the application. */
class NorRegisterLayer final {
public:
    int Initialize();
    int Read(std::uint8_t *buffer, std::uint32_t address, std::size_t size);
    int EnableMemoryMappedMode();
};

} // namespace uai::ai::nor::registers

#endif // UAI_AI_NOR_REGISTERS_HPP
