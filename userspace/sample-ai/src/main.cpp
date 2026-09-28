#include <cstdint>
#include <cstring>

#include <tk/tkernel.h>

#include "common/error.hpp"
#include "memory_allocator/memory_allocator.hpp"
#include "model_manager/model_manager.hpp"
#include "driver/npu_driver/debug.h"
#include "task/task_context.hpp"

/* C/C++境界: monitor/HAL とカーネル・ドライバーの C ABI 関数を
 * C++名修飾なしで呼び出すための宣言。実装は各SDK/カーネル側にある。 */
extern "C" {
#include <tm/tmonitor.h>
#include "stm32n6xx_hal.h"

extern DCMIPP_HandleTypeDef hcamera_dcmipp;

void NPU0_IRQHandler(UINT intno);
void IAC_IRQHandler(void);
/* カメラ診断値はaiのcamera driverが定義する。 */
extern volatile unsigned int g_camera_vsync_event_count;
extern volatile unsigned int g_camera_recovery_count;
extern volatile unsigned int g_camera_recovery_error_count;
extern volatile unsigned int g_camera_isp_error_count;
extern volatile unsigned int g_camera_dcmipp_last_status;
extern volatile unsigned int g_camera_dcmipp_error_count;
extern volatile unsigned int g_camera_camera_error_count;
extern volatile unsigned int g_camera_csi_last_status;
extern volatile unsigned int g_camera_csi_last_status1;
extern volatile unsigned int g_camera_csi_last_pending_status;
extern volatile unsigned int g_camera_csi_last_pending_status1;
extern volatile unsigned int g_camera_csi_error_count;
extern volatile unsigned int g_camera_csi_last_error_code;
extern volatile unsigned int g_camera_csi_sot_sync_dl0_count;
extern volatile unsigned int g_camera_csi_sot_sync_dl1_count;
extern volatile unsigned int g_camera_csi_sot_dl0_count;
extern volatile unsigned int g_camera_csi_sot_dl1_count;
extern volatile unsigned int g_ai_last_exposure_request_us;
extern volatile unsigned int g_ai_last_exposure_lines;
int32_t AiGetSensorGainMdB(void);
int32_t AiReadSensorRegisters(std::uint32_t *vmax,
                                   std::uint32_t *shutter,
                                   std::uint32_t *gain);
}
/* HALの共通診断値はcamera driver側で定義する。 */
extern "C" {
volatile std::uint32_t uai_hal_tick_calls = 0U;
volatile std::uint32_t uai_hal_tick_first = 0U;
volatile std::uint32_t uai_hal_tick_last = 0U;
volatile std::uint32_t uai_hal_tick_probe[4] = {};
volatile std::uint32_t uai_systick_count = 0U;
}

namespace uai::ai::task {

MemoryAllocator g_memory;
uai::ai::cache::CacheDriver g_cache;
uai::ai::psram::PsramDriver g_psram;
uai::ai::nor::NorDriver g_nor;
uai::ai::rif::RifDriver g_rif;
uai::ai::lcd::LcdDriver g_lcd;
uai::ai::camera::CameraDriver g_camera;
volatile std::uint32_t g_app_stage = 0U;
volatile bool g_external_nor_ready = false;
volatile bool g_model_switch_in_progress = false;
volatile std::uint32_t g_camera_reconfigure_request = 0U;
volatile std::uint32_t g_camera_reconfigure_complete = 0U;
volatile std::uint32_t g_camera_reconfigure_kind = 0U;
volatile std::uint32_t g_camera_reconfigure_code = 0U;
volatile std::uint32_t g_camera_reconfigure_detail = 0U;
ID g_external_memory_ready = 0;
ID g_frame_queue = 0;
ID g_box_queue = 0;
alignas(8) UB g_frame_queue_storage[
    sizeof(InferenceMessage) * kFrameQueueDepth] = {};
alignas(8) UB g_box_queue_storage[sizeof(BoxMessage) * kBoxQueueDepth] = {};
INT g_initialization_task_stack[
    kInitializationTaskStackSize / sizeof(INT)] = {};
INT g_camera_task_stack[kCameraTaskStackSize / sizeof(INT)] = {};
INT g_inference_task_stack[kInferenceTaskStackSize / sizeof(INT)] = {};

[[noreturn]] void Halt(const char *message)
{
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(message)));
    for (;;) {
        tk_dly_tsk(1000);
    }
}

bool IsBestEffort(ErrorCode code)
{
    return code == ErrorCode::kNoFrame || code == ErrorCode::kNoBuffer ||
           code == ErrorCode::kQueueFull;
}

Error InitializeDrivers()
{
    using common::ErrorCode;

    Error status = uai::ai::npu::NpuDriver::InitializeMemory();
    if (!status.Ok() && status.code != ErrorCode::kAlreadyInitialized) {
        return status;
    }
    status = g_cache.Initialize();
    if (!status.Ok() && status.code != ErrorCode::kAlreadyInitialized) {
        return status;
    }
    status = g_memory.Initialize();
    if (!status.Ok()) {
        return status;
    }

    status = g_rif.Initialize();
    if (!status.Ok() && status.code != ErrorCode::kAlreadyInitialized) {
        return status;
    }

    /* XSPI1/XSPI2 are RIF-protected on a cold boot. Configure their access
     * policy before the BSP touches either external memory. */
    if (!g_psram.Initialize()) {
        return {ErrorCode::kHardware, 0U, "psram.initialize"};
    }

    constexpr bool initialize_nor = kInferenceMode == InferenceMode::kNpu;
    int nor_status = -1;
    if (initialize_nor) {
        nor_status = g_nor.Initialize();
    } else {
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "boot: NOR skipped: inference disabled\n")));
    }
    g_external_nor_ready = nor_status == 0;

    status = g_lcd.Initialize(g_memory, g_cache);
    if (!status.Ok() && status.code != ErrorCode::kAlreadyInitialized) {
        return status;
    }
    status = g_camera.Initialize(g_memory, g_cache);
    if (!status.Ok() && status.code != ErrorCode::kAlreadyInitialized) {
        return status;
    }

    g_cache.KeepClocksOnSleep();
    g_psram.KeepClocksOnSleep();
    g_nor.KeepClocksOnSleep();
    uai::ai::npu::NpuDriver::KeepMemoryClocksOnSleep();
    g_lcd.KeepClocksOnSleep();
    g_camera.KeepClocksOnSleep();
    return {ErrorCode::kOk, static_cast<std::uint32_t>(nor_status),
            "main.initialize_drivers"};
}

std::uint32_t Now()
{
    SYSTIM time = {};
    return tk_get_otm(&time) == E_OK ? time.lo : 0U;
}

BoxSet EmptyBoxes()
{
    return {};
}

bool DrainLatestBoxes(BoxSet *active)
{
    BoxMessage message{};
    bool received = false;
    for (;;) {
        const INT size = tk_rcv_mbf(g_box_queue, &message, TMO_POL);
        if (size < 0) {
            break;
        }
        if (size == static_cast<INT>(sizeof(message))) {
            /* Keep the current display until the next inference result
             * arrives. The integrated result contains the latest state of all
             * models, so an empty result intentionally clears only the model
             * that just ran while preserving the other model states. */
            if (message.boxes.person_valid || message.boxes.face_valid ||
                message.boxes.segmentation_valid) {
                *active = message.boxes;
                active->model_sequence = message.boxes.model_sequence;
                active->capture_sequence = message.boxes.capture_sequence;
                received = true;
            }
        }
    }
    /* UART output is synchronous on the target. Do not put per-box logging in
     * the camera/LCD task during a normal run: it can delay the next display
     * composition at 115200 baud. */
#if AI_INFERENCE_DIAGNOSTICS
    if (received) {
        tm_printf(reinterpret_cast<const UB *>(
                  "lcd: box source=ai sequence=%u capture=%u count=%u\n"),
                  static_cast<unsigned int>(active->model_sequence),
                  static_cast<unsigned int>(active->capture_sequence),
                  static_cast<unsigned int>(active->person.count +
                                             active->face.count));
        tm_printf(reinterpret_cast<const UB *>(
                      "lcd: boxes person=%u face=%u mask_px=%u\n"),
                  static_cast<unsigned int>(active->person.count),
                  static_cast<unsigned int>(active->face.count),
                  static_cast<unsigned int>(
                      active->segmentation.mask_foreground_pixels));
    }
#endif
    return received;
}

void SendLatestBoxes(const BoxSet &boxes)
{
    BoxMessage message{};
    message.boxes = boxes;
    for (;;) {
        const ER error = tk_snd_mbf(g_box_queue, &message, sizeof(message),
                                    TMO_POL);
        if (error == E_OK) {
            return;
        }
        BoxMessage discarded{};
        if (tk_rcv_mbf(g_box_queue, &discarded, TMO_POL) < 0) {
            return;
        }
    }
}

void SendInferenceFrame(const InferenceFrame &frame)
{
    InferenceMessage message{};
    message.frame = frame;
    const ER error = tk_snd_mbf(g_frame_queue, &message, sizeof(message),
                                TMO_POL);
    if (error != E_OK) {
        const Error status = g_memory.ReleaseInferenceBuffer(frame);
        LogStatus("memory", status);
        tm_printf(reinterpret_cast<const UB *>(
                      "ai: frame dropped reason=queue_full sequence=%u\n"),
                  static_cast<unsigned int>(frame.capture_sequence));
    }
}

void ConfigureReferenceInterruptPriorities()
{
    /* The ref application normalizes all peripheral IRQ priorities to the
     * SysTick priority before it touches either external memory.  Preserve
     * that ordering because the image is launched from RAM and may inherit
     * stale priorities from the previous image. */
    uint32_t preempt_priority = 0U;
    uint32_t sub_priority = 0U;
    HAL_NVIC_GetPriority(SysTick_IRQn, HAL_NVIC_GetPriorityGrouping(),
                         &preempt_priority, &sub_priority);
    for (IRQn_Type irq = PVD_PVM_IRQn; irq <= LTDC_UP_ERR_IRQn;
         irq = static_cast<IRQn_Type>(static_cast<int32_t>(irq) + 1)) {
        HAL_NVIC_SetPriority(irq, preempt_priority, sub_priority);
    }
}

void StartTask(FP entry, INT *stack, SZ stack_size, PRI priority,
               const char *name)
{
    T_CTSK task = {};
    /* The stack argument is a statically allocated application stack.  Keep
     * it owned by the task instead of silently replacing it with a kernel
     * heap allocation. */
    task.tskatr = TA_HLNG | TA_USERBUF;
    task.task = entry;
    task.itskpri = priority;
    task.stksz = stack_size;
    task.bufptr = stack;
    const ID task_id = tk_cre_tsk(&task);
    if (task_id < E_OK) {
        tm_printf(reinterpret_cast<const UB *>(
                      "error: component=main operation=create_%s code=%x detail=0\n"),
                  name, static_cast<unsigned int>(task_id));
        Halt("ai: task create failed\n");
    }
    const ER error = tk_sta_tsk(task_id, 0);
    if (error != E_OK) {
        tm_printf(reinterpret_cast<const UB *>(
                      "error: component=main operation=start_%s code=%x detail=0\n"),
                  name, static_cast<unsigned int>(error));
        Halt("ai: task start failed\n");
    }
}

} // namespace uai::ai::task

using namespace uai::ai::task;

/* 割り込みベクタ/起動コードがこのC名で参照するハンドラー。 */
extern "C" void IAC_IRQHandler(void)
{
    const std::uint32_t flags0 = IAC->ISR[0];
    const std::uint32_t flags1 = IAC->ISR[1];
    const std::uint32_t flags2 = IAC->ISR[2];
    const std::uint32_t flags3 = IAC->ISR[3];
    const std::uint32_t flags4 = IAC->ISR[4];
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: IAC flags=%x,%x,%x,%x,%x\n"),
              static_cast<unsigned int>(flags0),
              static_cast<unsigned int>(flags1),
              static_cast<unsigned int>(flags2),
              static_cast<unsigned int>(flags3),
              static_cast<unsigned int>(flags4));
    if ((flags4 & 0x00400000U) != 0U) {
        tm_printf(reinterpret_cast<const UB *>(
                      "ai: RISAF12 iasr=%x iaesr=%x iaddr=%x\n"),
                  static_cast<unsigned int>(RISAF12->IASR),
                  static_cast<unsigned int>(RISAF12->IAR->IAESR),
                  static_cast<unsigned int>(RISAF12->IAR->IADDR));
    }
    HAL_RIF_IRQHandler();
}

/* µT-Kernelから呼び出されるaiのエントリーポイント。 */
extern "C" INT usermain(void)
{
    DumpCoreRegisters("usermain");

    /* µT-Kernel replaces the startup vector table with its RAM table. Use its
     * HLL wrapper for the NPU IRQ so the handler can signal the inference task
     * through an event flag. Keep the other vendor IRQ handlers direct because
     * they already provide the exception entry/return ABI. */
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
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: kernel interrupts npu=%x iac=%x\n"),
              static_cast<unsigned int>(npu_interrupt_status),
              static_cast<unsigned int>(iac_interrupt_status));
    if (npu_interrupt_status != E_OK || iac_interrupt_status != E_OK) {
        Halt("ai: interrupt registration failed\n");
    }
    DumpCoreRegisters("after_interrupts");

    T_CFLG flag = {};
    flag.flgatr = TA_TFIFO | TA_WSGL;
    g_external_memory_ready = tk_cre_flg(&flag);
    if (g_external_memory_ready < E_OK) {
        Halt("ai: event flag create failed\n");
    }

    T_CMBF frame_queue = {};
    frame_queue.mbfatr = TA_TFIFO;
    frame_queue.bufsz = sizeof(g_frame_queue_storage);
    frame_queue.maxmsz = sizeof(InferenceMessage);
    frame_queue.bufptr = g_frame_queue_storage;
    g_frame_queue = tk_cre_mbf(&frame_queue);
    if (g_frame_queue < E_OK) {
        Halt("ai: frame queue create failed\n");
    }

    T_CMBF box_queue = {};
    box_queue.mbfatr = TA_TFIFO;
    box_queue.bufsz = sizeof(g_box_queue_storage);
    box_queue.maxmsz = sizeof(BoxMessage);
    box_queue.bufptr = g_box_queue_storage;
    g_box_queue = tk_cre_mbf(&box_queue);
    if (g_box_queue < E_OK) {
        Halt("ai: box queue create failed\n");
    }

    StartTask(reinterpret_cast<FP>(application_initialize_task),
              g_initialization_task_stack, kInitializationTaskStackSize, 5,
              "application_initialize");

    for (;;) {
        tk_slp_tsk(TMO_FEVR);
    }
}
