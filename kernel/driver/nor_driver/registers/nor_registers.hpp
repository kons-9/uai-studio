#pragma once

#include <cstddef>
#include <cstdint>

namespace uai::ai::nor::registers {

struct Diagnostic {
    std::uint32_t code = 0;
    std::uint32_t stage = 0;
    std::int32_t result = 0;
    std::uint32_t error = 0;
    std::uint32_t state = 0;
    std::uint32_t status = 0;
    std::uint32_t control = 0;
    std::uint32_t command = 0;
};

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
    Diagnostic Diagnostics() const;
};

} // namespace uai::ai::nor::registers
