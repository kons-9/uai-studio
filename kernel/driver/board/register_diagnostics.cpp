#include "driver/board/register_diagnostics.hpp"

#include <cstdint>

#include "middleware/foundation/log.hpp"
#include "driver/npu_driver/debug.h"
#include "driver/npu_driver/registers/npu_registers.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
}

namespace uai::ai::driver::board {

void DumpCoreRegisters(const char *stage)
{
    if (!common::IsLogEnabled(common::LogLevel::kDebug)) {
        return;
    }
    const auto *vector_table =
        reinterpret_cast<volatile const std::uint32_t *>(SCB->VTOR);
    UAI_LOG_DEBUG("debug: core dump begin stage=%s\n", stage);
    UAI_LOG_DEBUG("debug: core vtor=%x msp=%x psp=%x control=%x ipsr=%x xpsr=%x\n",
              static_cast<unsigned int>(SCB->VTOR),
              static_cast<unsigned int>(__get_MSP()),
              static_cast<unsigned int>(__get_PSP()),
              static_cast<unsigned int>(__get_CONTROL()),
              static_cast<unsigned int>(__get_IPSR()),
              static_cast<unsigned int>(__get_xPSR()));
    UAI_LOG_DEBUG("debug: core primask=%x basepri=%x faultmask=%x icsr=%x shcsr=%x cfsr=%x hfsr=%x mmfar=%x bfar=%x ccr=%x\n",
              static_cast<unsigned int>(__get_PRIMASK()),
              static_cast<unsigned int>(__get_BASEPRI()),
              static_cast<unsigned int>(__get_FAULTMASK()),
              static_cast<unsigned int>(SCB->ICSR),
              static_cast<unsigned int>(SCB->SHCSR),
              static_cast<unsigned int>(SCB->CFSR),
              static_cast<unsigned int>(SCB->HFSR),
              static_cast<unsigned int>(SCB->MMFAR),
              static_cast<unsigned int>(SCB->BFAR),
              static_cast<unsigned int>(SCB->CCR));
    UAI_LOG_DEBUG("debug: core aircr=%x demcr=%x dwt_ctrl=%x dwt_cyccnt=%x\n",
              static_cast<unsigned int>(SCB->AIRCR),
              static_cast<unsigned int>(CoreDebug->DEMCR),
              static_cast<unsigned int>(DWT->CTRL),
              static_cast<unsigned int>(DWT->CYCCNT));
    UAI_LOG_DEBUG("debug: vector iac=%x npu=%x dcmipp=%x csi=%x ltdc=%x\n",
              static_cast<unsigned int>(vector_table[16U + IAC_IRQn]),
              static_cast<unsigned int>(vector_table[16U + NPU0_IRQn]),
              static_cast<unsigned int>(vector_table[16U + DCMIPP_IRQn]),
              static_cast<unsigned int>(vector_table[16U + CSI_IRQn]),
              static_cast<unsigned int>(vector_table[16U + LTDC_UP_ERR_IRQn]));

    const auto dump_irq = [](const char *name, IRQn_Type irq) {
        UAI_LOG_DEBUG("debug: irq %s n=%d en=%u pend=%u active=%u pri=%u\n",
                  name, static_cast<int>(irq),
                  static_cast<unsigned int>(NVIC_GetEnableIRQ(irq)),
                  static_cast<unsigned int>(NVIC_GetPendingIRQ(irq)),
                  static_cast<unsigned int>(NVIC_GetActive(irq)),
                  static_cast<unsigned int>(NVIC_GetPriority(irq)));
    };
    dump_irq("IAC", IAC_IRQn);
    dump_irq("NPU0", NPU0_IRQn);
    dump_irq("DCMIPP", DCMIPP_IRQn);
    dump_irq("CSI", CSI_IRQn);
    dump_irq("LTDC_UP_ERR", LTDC_UP_ERR_IRQn);
    UAI_LOG_DEBUG("debug: core dump end stage=%s\n", stage);
}

void DumpPeripheralRegisters(const char *stage)
{
    if (!common::IsLogEnabled(common::LogLevel::kDebug)) {
        return;
    }
    UAI_LOG_DEBUG("debug: peripheral dump begin stage=%s\n", stage);
    UAI_LOG_DEBUG("debug: rcc cr=%x csr=%x ahb5enr=%x ahb5ensr=%x memenr=%x memensr=%x ahb5rstr=%x ahb5rstsr=%x\n",
              static_cast<unsigned int>(RCC->CR),
              static_cast<unsigned int>(RCC->CSR),
              static_cast<unsigned int>(RCC->AHB5ENR),
              static_cast<unsigned int>(RCC->AHB5ENSR),
              static_cast<unsigned int>(RCC->MEMENR),
              static_cast<unsigned int>(RCC->MEMENSR),
              static_cast<unsigned int>(RCC->AHB5RSTR),
              static_cast<unsigned int>(RCC->AHB5RSTSR));
    UAI_LOG_DEBUG("debug: cache cr1=%x sr=%x ier=%x fcr=%x cr2=%x cmd_start=%x cmd_end=%x\n",
              static_cast<unsigned int>(CACHEAXI->CR1),
              static_cast<unsigned int>(CACHEAXI->SR),
              static_cast<unsigned int>(CACHEAXI->IER),
              static_cast<unsigned int>(CACHEAXI->FCR),
              static_cast<unsigned int>(CACHEAXI->CR2),
              static_cast<unsigned int>(CACHEAXI->CMDRSADDRR),
              static_cast<unsigned int>(CACHEAXI->CMDREADDRR));
    UAI_LOG_DEBUG("debug: iac ier=%x,%x,%x,%x,%x isr=%x,%x,%x,%x,%x\n",
              static_cast<unsigned int>(IAC->IER[0]),
              static_cast<unsigned int>(IAC->IER[1]),
              static_cast<unsigned int>(IAC->IER[2]),
              static_cast<unsigned int>(IAC->IER[3]),
              static_cast<unsigned int>(IAC->IER[4]),
              static_cast<unsigned int>(IAC->ISR[0]),
              static_cast<unsigned int>(IAC->ISR[1]),
              static_cast<unsigned int>(IAC->ISR[2]),
              static_cast<unsigned int>(IAC->ISR[3]),
              static_cast<unsigned int>(IAC->ISR[4]));
    UAI_LOG_DEBUG("debug: rifsc cr=%x seccfgr=%x,%x,%x,%x,%x,%x rimc=%x attr=%x\n",
              static_cast<unsigned int>(RIFSC->RISC_CR),
              static_cast<unsigned int>(RIFSC->RISC_SECCFGRx[0]),
              static_cast<unsigned int>(RIFSC->RISC_SECCFGRx[1]),
              static_cast<unsigned int>(RIFSC->RISC_SECCFGRx[2]),
              static_cast<unsigned int>(RIFSC->RISC_SECCFGRx[3]),
              static_cast<unsigned int>(RIFSC->RISC_SECCFGRx[4]),
              static_cast<unsigned int>(RIFSC->RISC_SECCFGRx[5]),
              static_cast<unsigned int>(RIFSC->RIMC_CR),
              static_cast<unsigned int>(RIFSC->RIMC_ATTRx[0]));
    UAI_LOG_DEBUG("debug: risaf12 cr=%x iasr=%x iacr=%x iaesr=%x iaddr=%x reg0=%x/%x/%x/%x\n",
              static_cast<unsigned int>(RISAF12->CR),
              static_cast<unsigned int>(RISAF12->IASR),
              static_cast<unsigned int>(RISAF12->IACR),
              static_cast<unsigned int>(RISAF12->IAR[0].IAESR),
              static_cast<unsigned int>(RISAF12->IAR[0].IADDR),
              static_cast<unsigned int>(RISAF12->REG[0].CFGR),
              static_cast<unsigned int>(RISAF12->REG[0].STARTR),
              static_cast<unsigned int>(RISAF12->REG[0].ENDR),
              static_cast<unsigned int>(RISAF12->REG[0].CIDCFGR));

    const auto npu_hardware =
        npu::registers::NpuRegisterLayer{}.ReadSnapshot();
    UAI_LOG_DEBUG("debug: npu epoch=%x/%x/%x irq=%x label=%x bc=%x int=%x/%x/%x bus=%x/%x\n",
              static_cast<unsigned int>(npu_hardware.epoch_control),
              static_cast<unsigned int>(npu_hardware.epoch_version),
              static_cast<unsigned int>(npu_hardware.epoch_address),
              static_cast<unsigned int>(npu_hardware.epoch_irq),
              static_cast<unsigned int>(npu_hardware.epoch_label),
              static_cast<unsigned int>(npu_hardware.epoch_byte_counter),
              static_cast<unsigned int>(npu_hardware.interrupt_control),
              static_cast<unsigned int>(npu_hardware.interrupt_status),
              static_cast<unsigned int>(npu_hardware.interrupt_or_mask),
              static_cast<unsigned int>(npu_hardware.busif0_control),
              static_cast<unsigned int>(npu_hardware.busif0_error));
    UAI_LOG_DEBUG("debug: npu stream ctrl=%x addr=%x fsize=%x depth=%x lim=%x/%x addr=%x cnt=%x/%x/%x/%x irq=%x\n",
              static_cast<unsigned int>(npu_hardware.stream0_control),
              static_cast<unsigned int>(npu_hardware.stream0_address),
              static_cast<unsigned int>(npu_hardware.stream0_frame_size),
              static_cast<unsigned int>(npu_hardware.stream0_depth),
              static_cast<unsigned int>(npu_hardware.stream0_limit_enable),
              static_cast<unsigned int>(npu_hardware.stream0_limit),
              static_cast<unsigned int>(npu_hardware.stream0_limit_address),
              static_cast<unsigned int>(npu_hardware.stream0_depth_count),
              static_cast<unsigned int>(npu_hardware.stream0_pixel_count),
              static_cast<unsigned int>(npu_hardware.stream0_line_count),
              static_cast<unsigned int>(npu_hardware.stream0_frame_count),
              static_cast<unsigned int>(npu_hardware.stream0_irq));
    UAI_LOG_DEBUG("debug: peripheral dump end stage=%s aton_irq=%u last=%x\n",
              stage, g_aton_irq_count, g_aton_last_irqs);
}

} // namespace uai::ai::driver::board