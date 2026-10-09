#pragma once

#include <cstddef>
#include <cstdint>

namespace uai::ai::nor::registers {

/* NOR controller/BSP access. Model policy belongs to the application. */
class NorRegisterLayer final {
public:
    int Initialize();
    int Read(
        std::uint8_t *buffer,
        std::uint32_t address,
        std::size_t size
    );
    int EnableMemoryMappedMode();
};

} // namespace uai::ai::nor::registers
