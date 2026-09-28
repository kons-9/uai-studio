#include "task/task_context.hpp"

namespace uai::ai::task {

namespace {
#if defined(AI_MODEL_FACE)
constexpr std::uintptr_t kModelDataAddress = 0x70800000UL;
constexpr const char *kModelDataAddressName = "70800000";
#elif defined(AI_MODEL_SEGMENTATION)
constexpr std::uintptr_t kModelDataAddress = 0x70600000UL;
constexpr const char *kModelDataAddressName = "70600000";
#else
constexpr std::uintptr_t kModelDataAddress = 0x70380000UL;
constexpr const char *kModelDataAddressName = "70380000";
#endif
} // namespace

void application_initialize_task(void)
{
    /* Resume the nominal HAL tick after pre-kernel setup. The sample-ai HAL
     * time bridge uses µT-Kernel time for HAL_GetTick/HAL_Delay. */
    HAL_ResumeTick();
    ConfigureReferenceInterruptPriorities();
    g_app_stage = 1U;
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "boot: external memory init begin\n")));

    const Error driver_status = InitializeDrivers();
    if (!driver_status.Ok()) {
        LogStatus("driver", driver_status);
        Halt("ai: driver initialization failed\n");
    }
    /* The XSPI NOR driver emits a long register snapshot on failure.
     * Keep other tasks from writing to the same T-Monitor UART while that
     * snapshot is being transferred, otherwise the diagnostic lines become
     * interleaved and unreadable.  Interrupts remain enabled, so HAL tick
     * timeouts used by the BSP continue to work. */
    if constexpr (kInferenceMode == InferenceMode::kNpu) {
        if (!g_external_nor_ready) {
            tm_printf(reinterpret_cast<const UB *>(
                          "ai: external NOR unavailable status=%d; inference disabled\n"),
                      static_cast<int>(driver_status.detail));
        }
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
            reinterpret_cast<const volatile std::uint32_t *>(kModelDataAddress);
        tm_printf(reinterpret_cast<const UB *>(
                      "boot: model data @%s=%x,%x,%x,%x\n"),
                  reinterpret_cast<const UB *>(
                      const_cast<char *>(kModelDataAddressName)),
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
