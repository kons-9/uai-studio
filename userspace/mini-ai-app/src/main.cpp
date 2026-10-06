#include <cstdint>

#include <tk/tkernel.h>

extern "C" {
#include "stm32n6xx_hal.h"

/* Defined by the NPU driver: wraps the Neural-ART handler and wakes the
 * inference task through an event flag. */
void NPU0_IRQHandler(UINT intno);
void IAC_IRQHandler(void);
}

#include "app_context.hpp"
#include "middleware/foundation/log.hpp"
#include "middleware/task/task.hpp"
#include "task/initialize_task.hpp"

/* Incremented by the µT-Kernel SysTick handler; the kernel declares it extern. */
extern "C" {
volatile std::uint32_t uai_systick_count = 0U;
}

namespace uai::ai::mini {

AppContext &App()
{
    static AppContext context;
    return context;
}

} // namespace uai::ai::mini

/* Illegal-access controller. A RIF violation (for example an NPU or DMA
 * access to a protected XSPI range) lands here; log it instead of faulting
 * silently. */
extern "C" void IAC_IRQHandler(void)
{
    const std::uint32_t flags4 = IAC->ISR[4];
    UAI_LOG_WARN("mini: IAC flags=%x,%x,%x,%x,%x\n",
                 static_cast<unsigned int>(IAC->ISR[0]),
                 static_cast<unsigned int>(IAC->ISR[1]),
                 static_cast<unsigned int>(IAC->ISR[2]),
                 static_cast<unsigned int>(IAC->ISR[3]),
                 static_cast<unsigned int>(flags4));
    if ((flags4 & 0x00400000U) != 0U) {
        UAI_LOG_ERROR("mini: RISAF12 iasr=%x iaesr=%x iaddr=%x\n",
                      static_cast<unsigned int>(RISAF12->IASR),
                      static_cast<unsigned int>(RISAF12->IAR->IAESR),
                      static_cast<unsigned int>(RISAF12->IAR->IADDR));
    }
    HAL_RIF_IRQHandler();
}

/* µT-Kernel calls this from its initial task after the kernel is up. */
extern "C" INT usermain(void)
{
    uai::ai::mini::AppContext &app = uai::ai::mini::App();

    const uai::ai::common::Error monitor_status = app.cpu_task_monitor.Start();
    if (!monitor_status.Ok()) {
        monitor_status.LogStatus("cpu_task_monitor.start");
        uai::ai::common::Task::Halt("mini: cpu task monitor start failed\n");
    }
    (void)app.cpu_task_monitor.RegisterTask(tk_get_tid(), "usermain");

    /* µT-Kernel owns the vector table, so peripheral interrupts are
     * registered through tk_def_int() rather than the CubeMX table. */
    T_DINT npu_interrupt = {};
    npu_interrupt.intatr = TA_HLNG;
    npu_interrupt.inthdr = reinterpret_cast<FP>(NPU0_IRQHandler);
    const ER npu_status = tk_def_int(static_cast<UINT>(NPU0_IRQn), &npu_interrupt);

    T_DINT iac_interrupt = {};
    iac_interrupt.intatr = TA_ASM;
    iac_interrupt.inthdr = reinterpret_cast<FP>(IAC_IRQHandler);
    const ER iac_status = tk_def_int(static_cast<UINT>(IAC_IRQn), &iac_interrupt);
    if (npu_status != E_OK || iac_status != E_OK) {
        UAI_LOG_ERROR("mini: interrupt registration npu=%x iac=%x\n",
                      static_cast<unsigned int>(npu_status),
                      static_cast<unsigned int>(iac_status));
        uai::ai::common::Task::Halt("mini: interrupt registration failed\n");
    }

    T_CFLG flag = {};
    flag.flgatr = TA_TFIFO | TA_WMUL;
    app.external_memory_ready = tk_cre_flg(&flag);
    if (app.external_memory_ready < E_OK) {
        uai::ai::common::Task::Halt("mini: event flag create failed\n");
    }
    if (!app.frames.Create().Ok() || !app.results.Create().Ok()) {
        uai::ai::common::Task::Halt("mini: channel create failed\n");
    }

    uai::ai::mini::InitializeTask::Instance().Start(app.cpu_task_monitor);

    for (;;) {
        tk_slp_tsk(TMO_FEVR);
    }
}
