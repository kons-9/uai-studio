#include "task/task_context.hpp"

namespace uai::ai::task {

void application_initialize_task(void)
{
    /* HAL tick is suspended while the pre-kernel clock tree is installed.
     * Resume it once µT-Kernel is running so Cube HAL timeout loops used by
     * the external-memory BSP can make progress. */
    HAL_ResumeTick();
    ConfigureReferenceInterruptPriorities();
    /* The RAM-launch trampoline disables D-cache to prevent stale lines from
     * the previous image overwriting the freshly programmed image.  Re-enable
     * it after C runtime/kernel startup, as the reference application does;
     * otherwise PSRAM frame copies and RGB conversion become prohibitively
     * slow and starve the display pipeline. */
    SCB_EnableDCache();
    tm_printf(reinterpret_cast<const UB *>(
                  "boot: dcache enabled ccr=%x\n"),
              static_cast<unsigned int>(SCB->CCR));
    g_app_stage = 1U;
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "boot: external memory init begin\n")));

    const Error memory_hardware_status = g_memory_hardware.Initialize();
    if (!memory_hardware_status.Ok()) {
        LogStatus("memory-hardware", memory_hardware_status);
        Halt("ai: memory hardware initialization failed\n");
    }
    const Error status = g_memory.Initialize();
    if (!status.Ok()) {
        LogStatus("memory", status);
        Halt("ai: memory initialization failed\n");
    }
    g_memory_hardware.KeepInferenceClocksOnSleep();
    /* Initialize external devices before applying the final RIF policy. */
    int nor_status = -1;
    const Error external_memory_status =
        g_memory_hardware.InitializeExternalMemory(&nor_status);
    if (!external_memory_status.Ok()) {
        LogStatus("memory-hardware", external_memory_status);
        Halt("ai: external memory initialization failed\n");
    }
    g_external_nor_ready = nor_status == 0;
    /* The XSPI NOR driver emits a long register snapshot on failure.
     * Keep other tasks from writing to the same T-Monitor UART while that
     * snapshot is being transferred, otherwise the diagnostic lines become
     * interleaved and unreadable.  Interrupts remain enabled, so HAL tick
     * timeouts used by the BSP continue to work. */
    if (!g_external_nor_ready) {
        tm_printf(reinterpret_cast<const UB *>(
                      "ai: external NOR unavailable status=%d; inference disabled\n"),
                  nor_status);
    }
    const Error access_status =
        g_memory_hardware.InitializePeripheralAccess();
    if (!access_status.Ok()) {
        LogStatus("memory", access_status);
        Halt("ai: peripheral access initialization failed\n");
    }
    g_app_stage = 2U;
    g_app_stage = 3U;
    g_app_stage = 4U;
    DumpPeripheralRegisters("after_access");
    tm_printf(reinterpret_cast<const UB *>(
                  "boot: npu cache init=%x enable=%x invalidate=%x cr1=%x sr=%x\n"),
              g_npu_cache_init_status, g_npu_cache_enable_status,
              g_npu_cache_invalidate_status, g_npu_cache_cr1,
              g_npu_cache_sr);
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "boot: external memory init result=ok detail=0\n")));
    if (g_external_nor_ready) {
        const volatile std::uint32_t *model_data =
            reinterpret_cast<const volatile std::uint32_t *>(0x70380000UL);
        tm_printf(reinterpret_cast<const UB *>(
                      "boot: model data @70380000=%x,%x,%x,%x\n"),
                  static_cast<unsigned int>(model_data[0]),
                  static_cast<unsigned int>(model_data[1]),
                  static_cast<unsigned int>(model_data[2]),
                  static_cast<unsigned int>(model_data[3]));
    } else {
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "boot: model data read skipped; NOR is not mapped\n")));
    }
    (void)tk_set_flg(g_external_memory_ready, kExternalMemoryReady);

    /* Keep the hardware initialization ahead of both application tasks, but
     * do the work on a dedicated stack rather than the small µT-Kernel
     * initial-task stack. */
    StartTask(reinterpret_cast<FP>(camera_render_task), g_camera_task_stack,
              kCameraTaskStackSize, 5, "camera_render");
    g_app_stage = 5U;
    if constexpr (kInferenceMode == InferenceMode::kNpu) {
        StartTask(reinterpret_cast<FP>(inference_task), g_inference_task_stack,
                  kInferenceTaskStackSize, 6, "inference");
    } else if constexpr (kInferenceMode == InferenceMode::kCopyOnly) {
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "ai: copy-only snapshot mode; NPU task disabled\n")));
    } else {
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "ai: inference task disabled for camera/CSI isolation\n")));
    }
    g_app_stage = 6U;

    for (;;) {
        tk_slp_tsk(TMO_FEVR);
    }
}

} // namespace uai::ai::task
