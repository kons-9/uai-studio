#include "task/task_context.hpp"

#include <cstdint>

#include <tk/tkernel.h>

extern "C" {
#include <tm/tmonitor.h>
#include "stm32n6xx_hal.h"
}

#include "driver/npu_driver/debug.h"
#include "driver/npu_driver/npu_driver.hpp"
#include "common/log.hpp"
#include "task/task_diagnostics.hpp"

namespace uai::ai::task {

namespace {
TaskContext g_context;
}

TaskContext &GetTaskContext()
{
    return g_context;
}

[[noreturn]] void TaskContext::Halt(const char *message)
{
    UAI_LOG_TEXT(uai::ai::common::LogLevel::kError,
                 reinterpret_cast<const UB *>(message));
    for (;;) {
        tk_dly_tsk(1000);
    }
}

bool TaskContext::IsBestEffort(common::ErrorCode code) const
{
    return code == common::ErrorCode::kNoFrame || code == common::ErrorCode::kNoBuffer ||
           code == common::ErrorCode::kQueueFull;
}

std::uint32_t TaskContext::Now() const
{
    SYSTIM time = {};
    return tk_get_otm(&time) == E_OK ? time.lo : 0U;
}

common::Error TaskContext::InitializeDrivers()
{

    common::Error status = uai::ai::npu::NpuDriver::InitializeMemory();
    if (!status.Ok() && status.code != common::ErrorCode::kAlreadyInitialized) {
        return status;
    }
    status = cache.Initialize();
    if (!status.Ok() && status.code != common::ErrorCode::kAlreadyInitialized) {
        return status;
    }
    status = memory.Initialize();
    if (!status.Ok()) {
        return status;
    }

    status = rif.Initialize();
    if (!status.Ok() && status.code != common::ErrorCode::kAlreadyInitialized) {
        return status;
    }

    /* XSPI1/XSPI2 are RIF-protected on a cold boot. Configure their access
     * policy before the BSP touches either external memory. */
    if (!psram.Initialize()) {
        return {common::ErrorCode::kHardware, 0U, "psram.initialize"};
    }

    constexpr bool initialize_nor = kInferenceMode == InferenceMode::kNpu;
    int nor_status = -1;
    if (initialize_nor) {
        nor_status = nor.Initialize();
    } else {
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "boot: NOR skipped: inference disabled\n"));
    }
    external_nor_ready = nor_status == 0;

    status = lcd.Initialize(memory, cache);
    if (!status.Ok() && status.code != common::ErrorCode::kAlreadyInitialized) {
        return status;
    }
    lcd.SetTimingDiagnostics(diagnostics.display_timing);
    status = camera.Initialize(memory, cache);
    if (!status.Ok() && status.code != common::ErrorCode::kAlreadyInitialized) {
        return status;
    }

    cache.KeepClocksOnSleep();
    psram.KeepClocksOnSleep();
    nor.KeepClocksOnSleep();
    uai::ai::npu::NpuDriver::KeepMemoryClocksOnSleep();
    lcd.KeepClocksOnSleep();
    camera.KeepClocksOnSleep();
    return {common::ErrorCode::kOk, static_cast<std::uint32_t>(nor_status),
            "task_context.initialize_drivers"};
}

void TaskContext::ConfigureReferenceInterruptPriorities()
{
    /* The ref application normalizes all peripheral IRQ priorities to the
     * SysTick priority before it touches either external memory. */
    uint32_t preempt_priority = 0U;
    uint32_t sub_priority = 0U;
    HAL_NVIC_GetPriority(SysTick_IRQn, HAL_NVIC_GetPriorityGrouping(),
                         &preempt_priority, &sub_priority);
    for (IRQn_Type irq = PVD_PVM_IRQn; irq <= LTDC_UP_ERR_IRQn;
         irq = static_cast<IRQn_Type>(static_cast<int32_t>(irq) + 1)) {
        HAL_NVIC_SetPriority(irq, preempt_priority, sub_priority);
    }
}

void TaskContext::CreateKernelObjects()
{
    T_CFLG flag = {};
    flag.flgatr = TA_TFIFO | TA_WSGL;
    external_memory_ready = tk_cre_flg(&flag);
    if (external_memory_ready < E_OK) {
        Halt("ai: event flag create failed\n");
    }

    T_CMBF frame_queue_config = {};
    frame_queue_config.mbfatr = TA_TFIFO;
    frame_queue_config.bufsz = sizeof(frame_queue_storage);
    frame_queue_config.maxmsz = sizeof(InferenceMessage);
    frame_queue_config.bufptr = frame_queue_storage;
    frame_queue = tk_cre_mbf(&frame_queue_config);
    if (frame_queue < E_OK) {
        Halt("ai: frame queue create failed\n");
    }

    T_CMBF box_queue_config = {};
    box_queue_config.mbfatr = TA_TFIFO;
    box_queue_config.bufsz = sizeof(box_queue_storage);
    box_queue_config.maxmsz = sizeof(BoxMessage);
    box_queue_config.bufptr = box_queue_storage;
    box_queue = tk_cre_mbf(&box_queue_config);
    if (box_queue < E_OK) {
        Halt("ai: box queue create failed\n");
    }

}

void TaskContext::StartApplicationTask(FP entry)
{
    StartTask(entry, initialization_task_stack, kInitializationTaskStackSize,
              5, "application_initialize");
}

void TaskContext::StartCameraTask(FP entry)
{
    StartTask(entry, camera_task_stack, kCameraTaskStackSize, 5,
              "camera_render");
}

void TaskContext::StartPersonFrameTask(FP entry)
{
    StartTask(entry, person_frame_task_stack, kPersonTaskStackSize, 4,
              "person_frame");
}

void TaskContext::StartPersonPreprocessTask(FP entry)
{
    StartTask(entry, person_preprocess_task_stack, kPersonTaskStackSize, 4,
              "person_preprocess");
}

void TaskContext::StartPersonNpuTask(FP entry)
{
    StartTask(entry, person_npu_task_stack, kPersonTaskStackSize, 4,
              "person_npu");
}

void TaskContext::StartPersonPostprocessTask(FP entry)
{
    StartTask(entry, person_postprocess_task_stack,
              kPersonPostprocessTaskStackSize, 6, "person_postprocess");
}

void TaskContext::StartTask(FP entry, INT *stack, SZ stack_size, PRI priority,
                            const char *name)
{
    T_CTSK task = {};
    task.tskatr = TA_HLNG | TA_USERBUF;
    task.task = entry;
    task.itskpri = priority;
    task.stksz = stack_size;
    task.bufptr = stack;
    const ID task_id = tk_cre_tsk(&task);
    if (task_id < E_OK) {
        UAI_LOG_ERROR(reinterpret_cast<const UB *>(
                          "error: component=task_context operation=create_%s code=%x detail=0\n"),
                      name, static_cast<unsigned int>(task_id));
        Halt("ai: task create failed\n");
    }
    const ER error = tk_sta_tsk(task_id, 0);
    if (error != E_OK) {
        UAI_LOG_ERROR(reinterpret_cast<const UB *>(
                          "error: component=task_context operation=start_%s code=%x detail=0\n"),
                      name, static_cast<unsigned int>(error));
        Halt("ai: task start failed\n");
    }
}

bool TaskContext::DrainLatestBoxes(memory_allocator::BoxSet *active)
{
    if (active == nullptr) {
        return false;
    }

    BoxMessage message{};
    bool received = false;
    for (;;) {
        const INT size = tk_rcv_mbf(box_queue, &message, TMO_POL);
        if (size < 0) {
            break;
        }
        if (size == static_cast<INT>(sizeof(message))) {
            /* Keep the current display until the next inference result. */
            if (message.boxes.person_valid || message.boxes.face_valid ||
                message.boxes.segmentation_valid) {
                *active = message.boxes;
                received = true;
            }
        }
    }

    if (received && diagnostics.display_trace) {
        UAI_LOG_DEBUG(reinterpret_cast<const UB *>(
                          "lcd: box source=ai sequence=%u capture=%u count=%u\n"),
                      static_cast<unsigned int>(active->model_sequence),
                      static_cast<unsigned int>(active->capture_sequence),
                      static_cast<unsigned int>(active->person.count +
                                                 active->face.count));
        UAI_LOG_DEBUG(reinterpret_cast<const UB *>(
                          "lcd: boxes person=%u face=%u mask_px=%u\n"),
                      static_cast<unsigned int>(active->person.count),
                      static_cast<unsigned int>(active->face.count),
                      static_cast<unsigned int>(
                          active->segmentation.mask_foreground_pixels));
    }
    return received;
}

void TaskContext::SendLatestBoxes(const memory_allocator::BoxSet &boxes)
{
    BoxMessage message{};
    message.boxes = boxes;
    for (;;) {
        const ER error = tk_snd_mbf(box_queue, &message, sizeof(message),
                                    TMO_POL);
        if (error == E_OK) {
            return;
        }
        BoxMessage discarded{};
        if (tk_rcv_mbf(box_queue, &discarded, TMO_POL) < 0) {
            return;
        }
    }
}

void TaskContext::SendInferenceFrame(const memory_allocator::InferenceFrame &frame)
{
    InferenceMessage message{};
    message.frame = frame;
    for (;;) {
        const ER error = tk_snd_mbf(frame_queue, &message, sizeof(message),
                                    TMO_POL);
        if (error == E_OK) {
            return;
        }

        /* Inference is slower than Pipe2.  Retaining FIFO frames makes every
         * subsequent result older than necessary, so discard the oldest
         * queued frame and keep the newest one instead. */
        InferenceMessage discarded{};
        const INT size = tk_rcv_mbf(frame_queue, &discarded, TMO_POL);
        if (size == static_cast<INT>(sizeof(discarded))) {
            const common::Error status =
                memory.ReleaseInferenceBuffer(discarded.frame);
            LogStatus("memory", status);
            continue;
        }

        const common::Error status = memory.ReleaseInferenceBuffer(frame);
        LogStatus("memory", status);
        UAI_LOG_DEBUG(reinterpret_cast<const UB *>(
                          "ai: frame dropped reason=queue_full sequence=%u\n"),
                      static_cast<unsigned int>(frame.capture_sequence));
        return;
    }
}

} // namespace uai::ai::task
