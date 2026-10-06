#include <cstdint>

#include <tk/tkernel.h>

extern "C" {
#include "stm32n6xx_hal.h"

void NPU0_IRQHandler(UINT intno);
void IAC_IRQHandler(void);
}

#include "task/application_initialize_task.hpp"
#include "task/task_context.hpp"
#include "task/task_diagnostics.hpp"
#include "middleware/foundation/log.hpp"

/* HAL time-bridge state is a C ABI surface used by the board support code. */
extern "C" {
volatile std::uint32_t uai_hal_tick_calls = 0U;
volatile std::uint32_t uai_hal_tick_first = 0U;
volatile std::uint32_t uai_hal_tick_last = 0U;
volatile std::uint32_t uai_hal_tick_probe[4] = {};
volatile std::uint32_t uai_systick_count = 0U;
}

/* 割り込みベクタ/起動コードがこのC名で参照するハンドラー。 */
extern "C" void IAC_IRQHandler(void)
{
    const std::uint32_t flags0 = IAC->ISR[0];
    const std::uint32_t flags1 = IAC->ISR[1];
    const std::uint32_t flags2 = IAC->ISR[2];
    const std::uint32_t flags3 = IAC->ISR[3];
    const std::uint32_t flags4 = IAC->ISR[4];
    if ((flags0 | flags1 | flags2 | flags3 | flags4) != 0U) {
        UAI_LOG_WARN("ai: IAC flags=%x,%x,%x,%x,%x\n",
                     static_cast<unsigned int>(flags0),
                     static_cast<unsigned int>(flags1),
                     static_cast<unsigned int>(flags2),
                     static_cast<unsigned int>(flags3),
                     static_cast<unsigned int>(flags4));
    }
    if ((flags4 & 0x00400000U) != 0U) {
        UAI_LOG_ERROR("ai: RISAF12 iasr=%x iaesr=%x iaddr=%x\n",
                      static_cast<unsigned int>(RISAF12->IASR),
                      static_cast<unsigned int>(RISAF12->IAR->IAESR),
                      static_cast<unsigned int>(RISAF12->IAR->IADDR));
    }
    HAL_RIF_IRQHandler();
}

/* µT-Kernelから呼び出されるaiのエントリーポイント。 */
extern "C" INT usermain(void)
{
    uai::ai::task::TaskContext &context = uai::ai::task::GetTaskContext();
    if (context.diagnostics.register_dump) {
        uai::ai::task::DumpCoreRegisters("usermain");
    }

    const uai::ai::common::Error cpu_monitor_status =
        context.cpu_task_monitor.Start();
    if (!cpu_monitor_status.Ok()) {
        UAI_LOG_ERROR("ai: cpu task monitor start failed code=%x detail=%x\n",
                      static_cast<unsigned int>(cpu_monitor_status.code),
                      static_cast<unsigned int>(cpu_monitor_status.detail));
        context.Halt("ai: cpu task monitor start failed\n");
    }
    (void)context.cpu_task_monitor.RegisterTask(tk_get_tid(), "usermain");

    /* µT-Kernel replaces the startup vector table with its RAM table. Use its
     * HLL wrapper for the NPU IRQ so the handler can signal the inference task
     * through an event flag. */
    T_DINT npu_interrupt = {};
    npu_interrupt.intatr = TA_HLNG;
    npu_interrupt.inthdr = reinterpret_cast<FP>(NPU0_IRQHandler);
    const ER npu_interrupt_status =
        tk_def_int(static_cast<UINT>(NPU0_IRQn), &npu_interrupt);

    T_DINT iac_interrupt = {};
    iac_interrupt.intatr = TA_ASM;
    iac_interrupt.inthdr = reinterpret_cast<FP>(IAC_IRQHandler);
    const ER iac_interrupt_status =
        tk_def_int(static_cast<UINT>(IAC_IRQn), &iac_interrupt);
    UAI_LOG_INFO("ai: kernel interrupts npu=%x iac=%x\n",
                 static_cast<unsigned int>(npu_interrupt_status),
                 static_cast<unsigned int>(iac_interrupt_status));
    if (npu_interrupt_status != E_OK || iac_interrupt_status != E_OK) {
        context.Halt("ai: interrupt registration failed\n");
    }
    if (context.diagnostics.register_dump) {
        uai::ai::task::DumpCoreRegisters("after_interrupts");
    }

    context.CreateKernelObjects();
    context.StartApplicationTask(
        reinterpret_cast<FP>(uai::ai::task::ApplicationInitializeTask::Entry));

    for (;;) {
        tk_slp_tsk(TMO_FEVR);
    }
}
