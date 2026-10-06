#ifndef UAI_AI_BOARD_REGISTER_DIAGNOSTICS_HPP
#define UAI_AI_BOARD_REGISTER_DIAGNOSTICS_HPP

namespace uai::ai::driver::board {

void DumpCoreRegisters(const char *stage);
void DumpPeripheralRegisters(const char *stage);

} // namespace uai::ai::driver::board

#endif