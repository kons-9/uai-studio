#pragma once

#include <cstdint>

namespace uai::ai::rif::registers {

/*
 * RISAF region-register offsets used by the AI RIF driver.
 *
 * The values are relative to RISAF_TypeDef::REG[0].  Keeping the register
 * map here makes the memory-protection configuration easier to review and
 * keeps the STM32 register knowledge out of the initialization flow.
 */
enum class RisafRegister : std::uint32_t {
    kConfiguration = 0x000U,    /* CFGR */
    kStartAddress = 0x004U,     /* STARTR */
    kEndAddress = 0x008U,       /* ENDR */
    kCidConfiguration = 0x00CU, /* CIDCFGR */
};

} // namespace uai::ai::rif::registers
