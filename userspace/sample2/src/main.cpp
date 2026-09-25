#include <cstdint>
#include <cstring>

#include <tk/tkernel.h>

#include "driver/imager_driver/imager_driver.hpp"
#include "common/error.hpp"
#include "driver/lcd_driver/lcd_driver.hpp"
#include "memory_manager/memory_hardware.hpp"
#include "memory_manager/memory_manager.hpp"
#include "model_manager/model_manager.hpp"
#include "driver/npu_driver/debug.h"
#include "driver/npu_driver/npu_hardware.hpp"

/* C/C++境界: monitor/HAL とカーネル・ドライバーの C ABI 関数を
 * C++名修飾なしで呼び出すための宣言。実装は各SDK/カーネル側にある。 */
extern "C" {
#include <tm/tmonitor.h>
#include "stm32n6xx_hal.h"

extern DCMIPP_HandleTypeDef hcamera_dcmipp;

void NPU0_IRQHandler(void);
void IAC_IRQHandler(void);
/* カメラ診断値はkernel/driver/src/arch/stm32n6570-dk/camera_driver_arch.cpp
 * が定義。フレームイベント数だけはこのファイル下部で定義している。 */
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
extern volatile unsigned int g_sample2_last_exposure_request_us;
extern volatile unsigned int g_sample2_last_exposure_lines;
int32_t Sample2GetSensorGainMdB(void);
int32_t Sample2ReadSensorRegisters(std::uint32_t *vmax,
                                   std::uint32_t *shutter,
                                   std::uint32_t *gain);
}

/* このファイルで実体を定義し、C側のHAL/割り込みコードから参照できる名前にする。 */
extern "C" {
volatile std::uint32_t uai_hal_tick_calls = 0U;
volatile std::uint32_t uai_hal_tick_first = 0U;
volatile std::uint32_t uai_hal_tick_last = 0U;
volatile std::uint32_t uai_hal_tick_probe[4] = {};
volatile std::uint32_t uai_systick_count = 0U;
/* カーネル側 camera_driver_arch.cpp が参照するフレームイベント数の実体。 */
volatile unsigned int g_camera_frame_event_count = 0U;
}

namespace {

using uai::sample2::memory_manager::BoxSet;
using uai::sample2::memory_manager::InferenceFrame;
using uai::sample2::memory_manager::MemoryManager;
using uai::sample2::memory_manager::MemoryHardware;
using uai::sample2::common::Error;
using uai::sample2::common::ErrorCode;
using NpuStatus = uai::sample2::npu_driver::Status;
using NpuHardware = uai::sample2::npu_driver::NpuHardware;

constexpr UINT kExternalMemoryReady = 0x01U;
constexpr std::uint32_t kInferencePeriod = 1000U;
constexpr std::uint32_t kBoxLifetimeMs = 3000U;
/* Isolate camera/CSI load from NPU inference while diagnosing link errors. */
enum class InferenceMode : std::uint8_t {
    kDisabled,
    kCopyOnly,
    kNpu,
};
enum class DisplayDiagnosticMode : std::uint8_t {
    kCameraPreview,
    kStaticPattern,
    kSyntheticCompose,
    kLiveCaptureFreeze,
};
/* Select kCopyOnly to measure the 691-KiB PSRAM snapshot copy without NPU
 * task, model loading, or inference execution. The default keeps AI disabled. */
constexpr InferenceMode kInferenceMode = InferenceMode::kDisabled;
/* Enable only for the static LCD/camera isolation A/B. This leaves the
 * currently running normal camera preview as the default image. */
constexpr DisplayDiagnosticMode kDisplayDiagnosticMode =
    DisplayDiagnosticMode::kLiveCaptureFreeze;
constexpr bool kDisplayCoordinatePatternDiagnostic =
    kDisplayDiagnosticMode == DisplayDiagnosticMode::kStaticPattern;
constexpr bool kSyntheticComposeDiagnostic =
    kDisplayDiagnosticMode == DisplayDiagnosticMode::kSyntheticCompose;
constexpr bool kLiveCaptureFreezeDiagnostic =
    kDisplayDiagnosticMode == DisplayDiagnosticMode::kLiveCaptureFreeze;
constexpr bool kCopyInferenceFrames =
    kInferenceMode != InferenceMode::kDisabled;
constexpr std::size_t kFrameQueueDepth = 4U;
constexpr std::size_t kBoxQueueDepth = 4U;
constexpr SZ kInitializationTaskStackSize = 32U * 1024U;
constexpr SZ kCameraTaskStackSize = 32U * 1024U;
constexpr SZ kInferenceTaskStackSize = 16U * 1024U;

struct InferenceMessage {
    InferenceFrame frame{};
};

struct BoxMessage {
    BoxSet boxes{};
};

static MemoryManager g_memory;
static MemoryHardware g_memory_hardware;
static volatile std::uint32_t g_app_stage = 0U;
static volatile bool g_external_nor_ready = false;
static ID g_external_memory_ready = 0;
static ID g_frame_queue = 0;
static ID g_box_queue = 0;
alignas(8) static UB g_frame_queue_storage[
    sizeof(InferenceMessage) * kFrameQueueDepth] = {};
alignas(8) static UB g_box_queue_storage[sizeof(BoxMessage) * kBoxQueueDepth] = {};
static INT g_initialization_task_stack[
    kInitializationTaskStackSize / sizeof(INT)] = {};
static INT g_camera_task_stack[kCameraTaskStackSize / sizeof(INT)] = {};
static INT g_inference_task_stack[kInferenceTaskStackSize / sizeof(INT)] = {};

void StartTask(FP entry, INT *stack, SZ stack_size, PRI priority,
               const char *name);
void camera_render_task(void);
void inference_task(void);

[[noreturn]] void Halt(const char *message)
{
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(message)));
    for (;;) {
        tk_dly_tsk(1000);
    }
}

void LogStatus(const char *component, const Error &error)
{
    if (!error.Ok()) {
        tm_printf(reinterpret_cast<const UB *>(
                      "error: component=%s operation=%s code=%s(%u) detail=%x\n"),
                  component, error.operation,
                  uai::sample2::common::ErrorCodeName(error.code),
                  static_cast<unsigned int>(error.code),
                  static_cast<unsigned int>(error.detail));
    }
}

void LogFrameBrightness(
    const uai::sample2::memory_manager::CaptureFrame &capture)
{
    if ((capture.sequence % 30U) != 0U) {
        return;
    }

    const Error ownership_status = g_memory.ValidateCaptureFrame(capture);
    if (!ownership_status.Ok()) {
        LogStatus("camera-luminance", ownership_status);
        return;
    }
    const Error cache_status =
        g_memory_hardware.PrepareForCpuRead(capture.buffer);
    if (!cache_status.Ok()) {
        LogStatus("camera-luminance", cache_status);
        return;
    }

    const auto *pixels =
        reinterpret_cast<const std::uint16_t *>(capture.buffer.address);
    constexpr std::size_t kPixelCount =
        uai::sample2::memory_manager::kFrameBytes / sizeof(*pixels);
    constexpr std::size_t kSampleStep = 64U;
    std::uint32_t luminance_sum = 0U;
    std::uint32_t luminance_peak = 0U;
    std::uint32_t sample_count = 0U;
    for (std::size_t i = 0U; i < kPixelCount; i += kSampleStep) {
        const std::uint16_t pixel = pixels[i];
        const std::uint32_t red = ((pixel >> 11U) & 0x1FU) * 255U / 31U;
        const std::uint32_t green = ((pixel >> 5U) & 0x3FU) * 255U / 63U;
        const std::uint32_t blue = (pixel & 0x1FU) * 255U / 31U;
        const std::uint32_t luminance =
            (77U * red + 150U * green + 29U * blue) >> 8U;
        luminance_sum += luminance;
        if (luminance > luminance_peak) {
            luminance_peak = luminance;
        }
        ++sample_count;
    }

    std::uint32_t sensor_vmax = 0U;
    std::uint32_t sensor_shutter = 0U;
    std::uint32_t sensor_gain = 0U;
    const int32_t sensor_register_status = Sample2ReadSensorRegisters(
        &sensor_vmax, &sensor_shutter, &sensor_gain);
    tm_printf(reinterpret_cast<const UB *>(
                  "camera: image brightness seq=%u mean=%u peak=%u exposure_us=%u exposure_lines=%u gain_mdB=%d regs=%d/%u,%u,%u\n"),
              static_cast<unsigned int>(capture.sequence),
              static_cast<unsigned int>(luminance_sum / sample_count),
              static_cast<unsigned int>(luminance_peak),
              g_sample2_last_exposure_request_us,
              g_sample2_last_exposure_lines,
              static_cast<int>(Sample2GetSensorGainMdB()),
              static_cast<int>(sensor_register_status),
              static_cast<unsigned int>(sensor_vmax),
              static_cast<unsigned int>(sensor_shutter),
              static_cast<unsigned int>(sensor_gain));
}

void LogNpuStatus(const NpuStatus &status)
{
    const auto &execution = status.execution;
    const auto &npu_hardware = execution.npu_hardware;
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: npu state=%u stai=%x epoch=%x addr=%x irq=%x label=%x bc=%x int=%x\n"),
              static_cast<unsigned int>(execution.state),
              static_cast<unsigned int>(execution.stai_status),
              static_cast<unsigned int>(npu_hardware.epoch_control),
              static_cast<unsigned int>(npu_hardware.epoch_address),
              static_cast<unsigned int>(npu_hardware.epoch_irq),
              static_cast<unsigned int>(npu_hardware.epoch_label),
              static_cast<unsigned int>(npu_hardware.epoch_byte_counter),
              static_cast<unsigned int>(npu_hardware.interrupt_status));
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: npu intctrl=%x or=%x and=%x bus=%x/%x stream=%x/%x/%x\n"),
              static_cast<unsigned int>(npu_hardware.interrupt_control),
              static_cast<unsigned int>(npu_hardware.interrupt_or_mask),
              static_cast<unsigned int>(npu_hardware.interrupt_and_mask),
              static_cast<unsigned int>(npu_hardware.busif0_control),
              static_cast<unsigned int>(npu_hardware.busif0_error),
              static_cast<unsigned int>(npu_hardware.stream0_control),
              static_cast<unsigned int>(npu_hardware.stream0_address),
              static_cast<unsigned int>(npu_hardware.stream0_irq));
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: npu stream_cfg fsize=%x depth=%x limiten=%x limit=%x limitaddr=%x cnt=%x/%x/%x/%x\n"),
              static_cast<unsigned int>(npu_hardware.stream0_frame_size),
              static_cast<unsigned int>(npu_hardware.stream0_depth),
              static_cast<unsigned int>(npu_hardware.stream0_limit_enable),
              static_cast<unsigned int>(npu_hardware.stream0_limit),
              static_cast<unsigned int>(npu_hardware.stream0_limit_address),
              static_cast<unsigned int>(npu_hardware.stream0_depth_count),
              static_cast<unsigned int>(npu_hardware.stream0_pixel_count),
              static_cast<unsigned int>(npu_hardware.stream0_line_count),
              static_cast<unsigned int>(npu_hardware.stream0_frame_count));
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: npu isr count=%u last=%x\n"),
              g_aton_irq_count, g_aton_last_irqs);
}

void DumpCoreRegisters(const char *stage)
{
    const auto *vector_table =
        reinterpret_cast<volatile const std::uint32_t *>(SCB->VTOR);
    tm_printf(reinterpret_cast<const UB *>(
                  "debug: core dump begin stage=%s\n"),
              stage);
    tm_printf(reinterpret_cast<const UB *>(
                  "debug: core vtor=%x msp=%x psp=%x control=%x ipsr=%x xpsr=%x\n"),
              static_cast<unsigned int>(SCB->VTOR),
              static_cast<unsigned int>(__get_MSP()),
              static_cast<unsigned int>(__get_PSP()),
              static_cast<unsigned int>(__get_CONTROL()),
              static_cast<unsigned int>(__get_IPSR()),
              static_cast<unsigned int>(__get_xPSR()));
    tm_printf(reinterpret_cast<const UB *>(
                  "debug: core primask=%x basepri=%x faultmask=%x icsr=%x shcsr=%x cfsr=%x hfsr=%x mmfar=%x bfar=%x ccr=%x\n"),
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
    tm_printf(reinterpret_cast<const UB *>(
                  "debug: core aircr=%x demcr=%x dwt_ctrl=%x dwt_cyccnt=%x\n"),
              static_cast<unsigned int>(SCB->AIRCR),
              static_cast<unsigned int>(CoreDebug->DEMCR),
              static_cast<unsigned int>(DWT->CTRL),
              static_cast<unsigned int>(DWT->CYCCNT));
    tm_printf(reinterpret_cast<const UB *>(
                  "debug: vector iac=%x npu=%x dcmipp=%x csi=%x ltdc=%x\n"),
              static_cast<unsigned int>(vector_table[16U + IAC_IRQn]),
              static_cast<unsigned int>(vector_table[16U + NPU0_IRQn]),
              static_cast<unsigned int>(vector_table[16U + DCMIPP_IRQn]),
              static_cast<unsigned int>(vector_table[16U + CSI_IRQn]),
                  static_cast<unsigned int>(vector_table[16U + LTDC_UP_ERR_IRQn]));

    const auto dump_irq = [](const char *name, IRQn_Type irq) {
        tm_printf(reinterpret_cast<const UB *>(
                      "debug: irq %s n=%d en=%u pend=%u active=%u pri=%u\n"),
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
    tm_printf(reinterpret_cast<const UB *>(
                  "debug: core dump end stage=%s\n"),
              stage);
}

void DumpPeripheralRegisters(const char *stage)
{
    tm_printf(reinterpret_cast<const UB *>(
                  "debug: peripheral dump begin stage=%s\n"),
              stage);
    tm_printf(reinterpret_cast<const UB *>(
                  "debug: rcc cr=%x csr=%x ahb5enr=%x ahb5ensr=%x memenr=%x memensr=%x ahb5rstr=%x ahb5rstsr=%x\n"),
              static_cast<unsigned int>(RCC->CR),
              static_cast<unsigned int>(RCC->CSR),
              static_cast<unsigned int>(RCC->AHB5ENR),
              static_cast<unsigned int>(RCC->AHB5ENSR),
              static_cast<unsigned int>(RCC->MEMENR),
              static_cast<unsigned int>(RCC->MEMENSR),
              static_cast<unsigned int>(RCC->AHB5RSTR),
              static_cast<unsigned int>(RCC->AHB5RSTSR));
    tm_printf(reinterpret_cast<const UB *>(
                  "debug: cache cr1=%x sr=%x ier=%x fcr=%x cr2=%x cmd_start=%x cmd_end=%x\n"),
              static_cast<unsigned int>(CACHEAXI->CR1),
              static_cast<unsigned int>(CACHEAXI->SR),
              static_cast<unsigned int>(CACHEAXI->IER),
              static_cast<unsigned int>(CACHEAXI->FCR),
              static_cast<unsigned int>(CACHEAXI->CR2),
              static_cast<unsigned int>(CACHEAXI->CMDRSADDRR),
              static_cast<unsigned int>(CACHEAXI->CMDREADDRR));
    tm_printf(reinterpret_cast<const UB *>(
                  "debug: iac ier=%x,%x,%x,%x,%x isr=%x,%x,%x,%x,%x\n"),
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
    tm_printf(reinterpret_cast<const UB *>(
                  "debug: rifsc cr=%x seccfgr=%x,%x,%x,%x,%x,%x rimc=%x attr=%x\n"),
              static_cast<unsigned int>(RIFSC->RISC_CR),
              static_cast<unsigned int>(RIFSC->RISC_SECCFGRx[0]),
              static_cast<unsigned int>(RIFSC->RISC_SECCFGRx[1]),
              static_cast<unsigned int>(RIFSC->RISC_SECCFGRx[2]),
              static_cast<unsigned int>(RIFSC->RISC_SECCFGRx[3]),
              static_cast<unsigned int>(RIFSC->RISC_SECCFGRx[4]),
              static_cast<unsigned int>(RIFSC->RISC_SECCFGRx[5]),
              static_cast<unsigned int>(RIFSC->RIMC_CR),
              static_cast<unsigned int>(RIFSC->RIMC_ATTRx[0]));
    tm_printf(reinterpret_cast<const UB *>(
                  "debug: risaf12 cr=%x iasr=%x iacr=%x iaesr=%x iaddr=%x reg0=%x/%x/%x/%x\n"),
              static_cast<unsigned int>(RISAF12->CR),
              static_cast<unsigned int>(RISAF12->IASR),
              static_cast<unsigned int>(RISAF12->IACR),
              static_cast<unsigned int>(RISAF12->IAR[0].IAESR),
              static_cast<unsigned int>(RISAF12->IAR[0].IADDR),
              static_cast<unsigned int>(RISAF12->REG[0].CFGR),
              static_cast<unsigned int>(RISAF12->REG[0].STARTR),
              static_cast<unsigned int>(RISAF12->REG[0].ENDR),
              static_cast<unsigned int>(RISAF12->REG[0].CIDCFGR));

    const auto npu_hardware = NpuHardware{}.ReadSnapshot();
    tm_printf(reinterpret_cast<const UB *>(
                  "debug: npu epoch=%x/%x/%x irq=%x label=%x bc=%x int=%x/%x/%x bus=%x/%x\n"),
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
    tm_printf(reinterpret_cast<const UB *>(
                  "debug: npu stream ctrl=%x addr=%x fsize=%x depth=%x lim=%x/%x addr=%x cnt=%x/%x/%x/%x irq=%x\n"),
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
    tm_printf(reinterpret_cast<const UB *>(
                  "debug: peripheral dump end stage=%s aton_irq=%u last=%x\n"),
              stage, g_aton_irq_count, g_aton_last_irqs);
}

bool IsBestEffort(ErrorCode code)
{
    return code == ErrorCode::kNoFrame || code == ErrorCode::kNoBuffer ||
           code == ErrorCode::kQueueFull;
}

std::uint32_t Now()
{
    SYSTIM time = {};
    return tk_get_otm(&time) == E_OK ? time.lo : 0U;
}

std::uint32_t Crc32Bytes(const std::uint8_t *bytes, std::size_t size)
{
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0U; i < size; ++i) {
        crc ^= bytes[i];
        for (std::uint32_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xEDB88320U : 0U);
        }
    }
    return ~crc;
}

void DumpFrozenCapture(const uai::sample2::memory_manager::CaptureFrame &frame)
{
    const auto *pixels = reinterpret_cast<const std::uint16_t *>(
        frame.buffer.address);
    const std::uint32_t full_crc = Crc32Bytes(
        reinterpret_cast<const std::uint8_t *>(frame.buffer.address),
        frame.buffer.size);
    tm_printf(reinterpret_cast<const UB *>(
                  "camera: frozen raw sequence=%u address=%x bytes=%u crc=%x frames=%u cptact=%x m0ar=%x\n"),
              static_cast<unsigned int>(frame.sequence),
              static_cast<unsigned int>(frame.buffer.address),
              static_cast<unsigned int>(frame.buffer.size), full_crc,
              g_camera_frame_event_count,
              static_cast<unsigned int>(
                  DCMIPP->CMSR1 & DCMIPP_CMSR1_P1CPTACT),
              static_cast<unsigned int>(HAL_DCMIPP_PIPE_GetMemoryAddress(
                  &hcamera_dcmipp, DCMIPP_PIPE1,
                  DCMIPP_MEMORY_ADDRESS_0)));
    tm_printf(reinterpret_cast<const UB *>(
                  "camera: P1 regs fscr=%x fctcr=%x sr=%x crop=%x/%x down=%x/%x pack=%x pitch=%x m0=%x/%x csi=%x/%x counts=%u sot_sync=%u/%u sot=%u/%u pend=%x/%x\n"),
              static_cast<unsigned int>(DCMIPP->P1FSCR),
              static_cast<unsigned int>(DCMIPP->P1FCTCR),
              static_cast<unsigned int>(DCMIPP->P1SR),
              static_cast<unsigned int>(DCMIPP->P1CRSTR),
              static_cast<unsigned int>(DCMIPP->P1CRSZR),
              static_cast<unsigned int>(DCMIPP->P1DSRTIOR),
              static_cast<unsigned int>(DCMIPP->P1DSSZR),
              static_cast<unsigned int>(DCMIPP->P1PPCR),
              static_cast<unsigned int>(DCMIPP->P1PPM0PR),
              static_cast<unsigned int>(DCMIPP->P1PPM0AR1),
              static_cast<unsigned int>(DCMIPP->P1PPM0AR2),
              static_cast<unsigned int>(CSI->SR0),
              static_cast<unsigned int>(CSI->SR1),
              g_camera_csi_error_count,
              g_camera_csi_sot_sync_dl0_count,
              g_camera_csi_sot_sync_dl1_count,
              g_camera_csi_sot_dl0_count,
              g_camera_csi_sot_dl1_count,
              g_camera_csi_last_pending_status,
              g_camera_csi_last_pending_status1);
    tm_printf(reinterpret_cast<const UB *>(
                  "camera: P1 detail cfscr=%x cfctcr=%x decr=%x dmcr=%x "
                  "cdccr=%x cdscr=%x cdsrtior=%x cdsszr=%x "
                  "cppcr=%x cppitch=%x cpm0=%x/%x\n"),
              static_cast<unsigned int>(DCMIPP->P1CFSCR),
              static_cast<unsigned int>(DCMIPP->P1CFCTCR),
              static_cast<unsigned int>(DCMIPP->P1DECR),
              static_cast<unsigned int>(DCMIPP->P1DMCR),
              static_cast<unsigned int>(DCMIPP->P1CDCCR),
              static_cast<unsigned int>(DCMIPP->P1CDSCR),
              static_cast<unsigned int>(DCMIPP->P1CDSRTIOR),
              static_cast<unsigned int>(DCMIPP->P1CDSSZR),
              static_cast<unsigned int>(DCMIPP->P1CPPCR),
              static_cast<unsigned int>(DCMIPP->P1CPPM0PR),
              static_cast<unsigned int>(DCMIPP->P1CPPM0AR1),
              static_cast<unsigned int>(DCMIPP->P1CPPM0AR2));
    tm_printf(reinterpret_cast<const UB *>(
                  "camera: CSI cfg pfcr=%x pcr=%x vc0=%x/%x/%x/%x\n"),
              static_cast<unsigned int>(CSI->PFCR),
              static_cast<unsigned int>(CSI->PCR),
              static_cast<unsigned int>(CSI->VC0CFGR1),
              static_cast<unsigned int>(CSI->VC0CFGR2),
              static_cast<unsigned int>(CSI->VC0CFGR3),
              static_cast<unsigned int>(CSI->VC0CFGR4));

    std::uint32_t zero_rows = 0U;
    std::uint32_t nonzero_rows = 0U;
    std::uint32_t first_nonzero =
        uai::sample2::memory_manager::kFrameHeight;
    std::uint32_t last_nonzero = 0U;
    std::uint32_t distinct_row_crcs = 0U;
    std::uint32_t row_crcs[uai::sample2::memory_manager::kFrameHeight] = {};
    for (std::uint32_t y = 0U;
         y < uai::sample2::memory_manager::kFrameHeight; ++y) {
        const auto *row = pixels +
                          y * uai::sample2::memory_manager::kFrameWidth;
        bool any_nonzero = false;
        for (std::uint32_t x = 0U;
             x < uai::sample2::memory_manager::kFrameWidth; ++x) {
            any_nonzero = any_nonzero || row[x] != 0U;
        }
        row_crcs[y] = Crc32Bytes(
            reinterpret_cast<const std::uint8_t *>(row),
            uai::sample2::memory_manager::kFrameWidth * 2U);
        if (any_nonzero) {
            ++nonzero_rows;
            first_nonzero = first_nonzero < y ? first_nonzero : y;
            last_nonzero = y;
        } else {
            ++zero_rows;
        }
        bool seen = false;
        for (std::uint32_t previous = 0U; previous < y; ++previous) {
            if (row_crcs[previous] == row_crcs[y]) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            ++distinct_row_crcs;
        }
    }
    tm_printf(reinterpret_cast<const UB *>(
                  "camera: frozen row distribution nonzero=%u zero=%u first=%u last=%u distinct_crc=%u\n"),
              nonzero_rows, zero_rows,
              nonzero_rows == 0U ? 0U : first_nonzero,
              nonzero_rows == 0U ? 0U : last_nonzero, distinct_row_crcs);
    std::uint32_t reported_nonzero = 0U;
    for (std::uint32_t y = 0U;
         y < uai::sample2::memory_manager::kFrameHeight &&
         reported_nonzero < 24U;
         ++y) {
        const auto *row = pixels +
                          y * uai::sample2::memory_manager::kFrameWidth;
        bool any_nonzero = false;
        for (std::uint32_t x = 0U;
             x < uai::sample2::memory_manager::kFrameWidth; ++x) {
            any_nonzero = any_nonzero || row[x] != 0U;
        }
        if (any_nonzero) {
            tm_printf(reinterpret_cast<const UB *>(
                          "camera: frozen nonzero row y=%u crc=%x\n"),
                      y, row_crcs[y]);
            ++reported_nonzero;
        }
    }

    constexpr std::uint32_t kRows[] = {0U, 1U, 35U, 194U, 240U, 400U, 479U};
    constexpr std::uint32_t kColumns[] = {0U, 1U, 16U, 39U, 40U, 799U};
    for (const std::uint32_t y : kRows) {
        const auto *row_bytes = reinterpret_cast<const std::uint8_t *>(
            pixels + y * uai::sample2::memory_manager::kFrameWidth);
        const std::uint32_t row_crc = Crc32Bytes(
            row_bytes, uai::sample2::memory_manager::kFrameWidth * 2U);
        tm_printf(reinterpret_cast<const UB *>(
                      "camera: frozen row y=%u crc=%x samples="),
                  y, row_crc);
        for (const std::uint32_t x : kColumns) {
            tm_printf(reinterpret_cast<const UB *>("%04x%s"),
                      static_cast<unsigned int>(
                          pixels[y * uai::sample2::memory_manager::kFrameWidth +
                                 x]),
                      x == kColumns[sizeof(kColumns) / sizeof(kColumns[0]) - 1U]
                          ? "\n"
                          : ",");
        }
    }
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
            *active = message.boxes;
            received = true;
        }
    }
    if (received) {
        tm_printf(reinterpret_cast<const UB *>(
                      "lcd: box source=ai sequence=%u capture=%u count=%u\n"),
                  static_cast<unsigned int>(active->model_sequence),
                  static_cast<unsigned int>(active->capture_sequence),
                  static_cast<unsigned int>(active->count));
        const std::uint32_t count =
            active->count < uai::sample2::memory_manager::kMaxBoxes
                ? active->count
                : uai::sample2::memory_manager::kMaxBoxes;
        for (std::uint32_t i = 0U; i < count; ++i) {
            const auto &box = active->boxes[i];
            const auto confidence_milli = box.confidence > 0.0F
                                              ? static_cast<std::uint32_t>(
                                                    box.confidence * 1000.0F +
                                                    0.5F)
                                              : 0U;
            tm_printf(reinterpret_cast<const UB *>(
                          "lcd: box active index=%u x=%d y=%d w=%d h=%d conf_milli=%u\n"),
                      static_cast<unsigned int>(i),
                      static_cast<int>(box.x), static_cast<int>(box.y),
                      static_cast<int>(box.width),
                      static_cast<int>(box.height),
                      static_cast<unsigned int>(confidence_milli));
        }
    }
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
        Halt("sample2: memory hardware initialization failed\n");
    }
    const Error status = g_memory.Initialize();
    if (!status.Ok()) {
        LogStatus("memory", status);
        Halt("sample2: memory initialization failed\n");
    }
    g_memory_hardware.KeepInferenceClocksOnSleep();
    /* Initialize external devices before applying the final RIF policy. */
    int nor_status = -1;
    const Error external_memory_status =
        g_memory_hardware.InitializeExternalMemory(&nor_status);
    if (!external_memory_status.Ok()) {
        LogStatus("memory-hardware", external_memory_status);
        Halt("sample2: external memory initialization failed\n");
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
        Halt("sample2: peripheral access initialization failed\n");
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

void camera_render_task(void)
{
    Error status{};

    uai::sample2::LcdDriver lcd;
    uai::sample2::ImagerDriver imager;
    const BoxSet initial = EmptyBoxes();

    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "lcd: initialize begin\n")));
    status = lcd.Initialize(g_memory, g_memory_hardware);
    if (!status.Ok()) {
        LogStatus("lcd", status);
        Halt("sample2: lcd initialization failed\n");
    }
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "lcd: initialize result=ok\n")));
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "lcd: initial frame begin\n")));
    status = lcd.ShowInitialFrame(initial,
                                  kDisplayCoordinatePatternDiagnostic);
    if (!status.Ok()) {
        LogStatus("lcd", status);
        Halt("sample2: initial frame failed\n");
    }
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "lcd: initial buffer ready box_count=0\n")));

    if constexpr (kDisplayCoordinatePatternDiagnostic) {
        status = lcd.SynchronizeCurrentFrame();
        if (!status.Ok()) {
            LogStatus("lcd", status);
            Halt("sample2: diagnostic frame did not latch\n");
        }
        tm_printf(reinterpret_cast<const UB *>(
                      "lcd: diagnostic readback fb=%x cr=%x pfcr=%x cfblr=%x cfblnr=%x awcr=%x twcr=%x isr=%x\n"),
                  static_cast<unsigned int>(LTDC_Layer1->CFBAR),
                  static_cast<unsigned int>(LTDC_Layer1->CR),
                  static_cast<unsigned int>(LTDC_Layer1->PFCR),
                  static_cast<unsigned int>(LTDC_Layer1->CFBLR),
                  static_cast<unsigned int>(LTDC_Layer1->CFBLNR),
                  static_cast<unsigned int>(LTDC->AWCR),
                  static_cast<unsigned int>(LTDC->TWCR),
                  static_cast<unsigned int>(LTDC->ISR));
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "lcd: static coordinate pattern active; camera remains stopped\n")));
        for (;;) {
            tk_dly_tsk(1000);
        }
    }

    if constexpr (kSyntheticComposeDiagnostic) {
        status = lcd.SynchronizeCurrentFrame();
        if (!status.Ok()) {
            LogStatus("lcd", status);
            Halt("sample2: initial diagnostic frame did not latch\n");
        }

        std::uintptr_t capture0 = 0U;
        std::uintptr_t capture1 = 0U;
        status = g_memory.CaptureBuffers(&capture0, &capture1);
        if (!status.Ok()) {
            LogStatus("memory", status);
            Halt("sample2: diagnostic capture buffers unavailable\n");
        }
        const uai::sample2::memory_manager::Buffer source_buffer{
            capture0, uai::sample2::memory_manager::kFrameBytes, 0U,
            uai::sample2::memory_manager::Region::kCapture};
        status = g_memory_hardware.PrepareForDmaWrite(source_buffer);
        if (!status.Ok()) {
            LogStatus("memory", status);
            Halt("sample2: diagnostic source cache prepare failed\n");
        }
        status = lcd.GenerateCoordinatePattern(source_buffer);
        if (!status.Ok()) {
            LogStatus("lcd", status);
            Halt("sample2: diagnostic pattern generation failed\n");
        }
        SCB_CleanDCache_by_Addr(
            reinterpret_cast<std::uint32_t *>(source_buffer.address),
            static_cast<std::int32_t>(source_buffer.size));

        uai::sample2::memory_manager::CaptureFrame synthetic_capture{};
        status = g_memory.ImportCompletedCapture(source_buffer.address,
                                                  &synthetic_capture);
        if (!status.Ok()) {
            LogStatus("memory", status);
            Halt("sample2: diagnostic capture import failed\n");
        }
        tm_printf(reinterpret_cast<const UB *>(
                      "lcd: synthetic compose begin sequence=%u source=%x bytes=%u\n"),
                  static_cast<unsigned int>(synthetic_capture.sequence),
                  static_cast<unsigned int>(source_buffer.address),
                  static_cast<unsigned int>(source_buffer.size));
        status = lcd.ComposeAndPresent(synthetic_capture, initial, true);
        if (!status.Ok()) {
            LogStatus("lcd", status);
            Halt("sample2: synthetic compose failed\n");
        }
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "lcd: synthetic compose presented; camera remains stopped\n")));
        for (;;) {
            tk_dly_tsk(1000);
        }
    }

    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "camera: initialize begin\n")));
    status = imager.Initialize(g_memory, g_memory_hardware);
    if (!status.Ok()) {
        LogStatus("camera", status);
        Halt("sample2: camera initialization failed\n");
    }
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "camera: initialize result=ok\n")));
    status = imager.Start();
    if (!status.Ok()) {
        LogStatus("camera", status);
        Halt("sample2: camera start failed\n");
    }
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "camera: start result=ok detail=0\n")));
    if constexpr (kLiveCaptureFreezeDiagnostic) {
        uai::sample2::memory_manager::CaptureFrame first_capture{};
        const std::uint32_t wait_start = Now();
        for (;;) {
            status = imager.Process();
            if (!status.Ok()) {
                LogStatus("camera", status);
                Halt("sample2: frozen capture process failed\n");
            }
            uai::sample2::memory_manager::CaptureFrame candidate{};
            status = imager.TakeCompletedCapture(&candidate);
            if (status.Ok()) {
                if ((candidate.sequence % 10U) == 0U) {
                    LogFrameBrightness(candidate);
                    tm_printf(reinterpret_cast<const UB *>(
                                  "camera: freeze warmup sequence=%u events=%u buffer=%x\n"),
                              static_cast<unsigned int>(candidate.sequence),
                              g_camera_frame_event_count,
                              static_cast<unsigned int>(candidate.buffer.address));
                }
                if (candidate.sequence >= 30U) {
                    first_capture = candidate;
                    break;
                }
                continue;
            }
            if (status.code != ErrorCode::kNoFrame) {
                LogStatus("camera", status);
                Halt("sample2: frozen capture acquire failed\n");
            }
            if (static_cast<std::uint32_t>(Now() - wait_start) >= 3000U) {
                Halt("sample2: frozen capture timed out\n");
            }
            tk_dly_tsk(1);
        }
        const unsigned int events_before_stop = g_camera_frame_event_count;
        tm_printf(reinterpret_cast<const UB *>(
                      "camera: freeze requested sequence=%u address=%x events=%u\n"),
                  static_cast<unsigned int>(first_capture.sequence),
                  static_cast<unsigned int>(first_capture.buffer.address),
                  events_before_stop);
        status = imager.Stop();
        if (!status.Ok()) {
            LogStatus("camera", status);
            Halt("sample2: camera stop failed; capture not inspected\n");
        }

        uai::sample2::memory_manager::CaptureFrame latest_capture{};
        const Error latest_status =
            imager.TakeCompletedCapture(&latest_capture);
        if (latest_status.Ok()) {
            first_capture = latest_capture;
        } else if (latest_status.code != ErrorCode::kNoFrame) {
            LogStatus("camera", latest_status);
            Halt("sample2: stopped capture acquire failed\n");
        }
        status = g_memory_hardware.PrepareForCpuRead(first_capture.buffer);
        if (!status.Ok()) {
            LogStatus("memory", status);
            Halt("sample2: frozen capture cache invalidate failed\n");
        }
        tm_printf(reinterpret_cast<const UB *>(
                      "camera: freeze complete events_before=%u events_after=%u selected_sequence=%u selected_address=%x\n"),
                  events_before_stop, g_camera_frame_event_count,
                  static_cast<unsigned int>(first_capture.sequence),
                  static_cast<unsigned int>(first_capture.buffer.address));
        DumpFrozenCapture(first_capture);

        status = lcd.ComposeAndPresent(first_capture, initial, true);
        if (!status.Ok()) {
            LogStatus("lcd", status);
            Halt("sample2: frozen live capture compose failed\n");
        }
        status = lcd.SynchronizeCurrentFrame();
        if (!status.Ok()) {
            LogStatus("lcd", status);
            Halt("sample2: frozen live capture display did not latch\n");
        }
        tm_printf(reinterpret_cast<const UB *>(
                      "lcd: frozen capture readback fb=%x cr=%x pfcr=%x cfblr=%x cfblnr=%x awcr=%x twcr=%x isr=%x\n"),
                  static_cast<unsigned int>(LTDC_Layer1->CFBAR),
                  static_cast<unsigned int>(LTDC_Layer1->CR),
                  static_cast<unsigned int>(LTDC_Layer1->PFCR),
                  static_cast<unsigned int>(LTDC_Layer1->CFBLR),
                  static_cast<unsigned int>(LTDC_Layer1->CFBLNR),
                  static_cast<unsigned int>(LTDC->AWCR),
                  static_cast<unsigned int>(LTDC->TWCR),
                  static_cast<unsigned int>(LTDC->ISR));
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "camera: frozen raw capture displayed; camera stopped\n")));
        for (;;) {
            tk_dly_tsk(1000);
        }
    }
    tm_printf(reinterpret_cast<const UB *>(
                  "camera: irq dcmipp=%u/%u csi=%u/%u\n"),
              static_cast<unsigned int>(NVIC_GetEnableIRQ(DCMIPP_IRQn)),
              static_cast<unsigned int>(NVIC_GetPendingIRQ(DCMIPP_IRQn)),
              static_cast<unsigned int>(NVIC_GetEnableIRQ(CSI_IRQn)),
              static_cast<unsigned int>(NVIC_GetPendingIRQ(CSI_IRQn)));

    BoxSet active_boxes = initial;
    bool first_frame = true;
    std::uint32_t next_inference = 0U;

    std::uint32_t loop_count = 0U;
    unsigned int reported_pipe_errors = 0U;
    unsigned int reported_camera_errors = 0U;
    unsigned int reported_csi_errors = 0U;
    unsigned int reported_recoveries = 0U;
    unsigned int reported_recovery_errors = 0U;
    unsigned int reported_isp_errors = 0U;
    std::uint32_t last_async_error_log_tick = Now();
    std::uint32_t last_box_update = Now();
    for (;;) {
        ++loop_count;
        status = imager.Process();
        if (!status.Ok()) {
            LogStatus("camera", status);
            Halt("sample2: camera process failed\n");
        }
        const bool async_error_changed =
            reported_pipe_errors != g_camera_dcmipp_error_count ||
            reported_camera_errors != g_camera_camera_error_count ||
            reported_csi_errors != g_camera_csi_error_count ||
            reported_isp_errors != g_camera_isp_error_count;
        if (async_error_changed &&
            ((reported_pipe_errors == 0U &&
              reported_camera_errors == 0U && reported_csi_errors == 0U &&
              reported_isp_errors == 0U) ||
             Now() - last_async_error_log_tick >= 1000U)) {
            reported_pipe_errors = g_camera_dcmipp_error_count;
            reported_camera_errors = g_camera_camera_error_count;
            reported_csi_errors = g_camera_csi_error_count;
            reported_isp_errors = g_camera_isp_error_count;
            last_async_error_log_tick = Now();
            tm_printf(reinterpret_cast<const UB *>(
                          "camera: async errors pipe=%u sensor=%u csi=%u isp=%u dcmipp=%x csi0=%x csi1=%x pend0=%x pend1=%x code=%x sot_sync_dl0=%u sot_sync_dl1=%u sot_dl0=%u sot_dl1=%u\n"),
                      reported_pipe_errors, reported_camera_errors,
                      reported_csi_errors, reported_isp_errors,
                      g_camera_dcmipp_last_status,
                      g_camera_csi_last_status,
                      g_camera_csi_last_status1,
                      g_camera_csi_last_pending_status,
                      g_camera_csi_last_pending_status1,
                      g_camera_csi_last_error_code,
                      g_camera_csi_sot_sync_dl0_count,
                      g_camera_csi_sot_sync_dl1_count,
                      g_camera_csi_sot_dl0_count,
                      g_camera_csi_sot_dl1_count);
        }
        if (reported_recoveries != g_camera_recovery_count ||
            reported_recovery_errors != g_camera_recovery_error_count) {
            reported_recoveries = g_camera_recovery_count;
            reported_recovery_errors = g_camera_recovery_error_count;
            tm_printf(reinterpret_cast<const UB *>(
                          "camera: recovery attempts=%u failed=%u frames=%u vsync=%u\n"),
                      reported_recoveries, reported_recovery_errors,
                      g_camera_frame_event_count, g_camera_vsync_event_count);
        }

        const std::uint32_t now = Now();
        if (DrainLatestBoxes(&active_boxes)) {
            last_box_update = now;
        } else if (active_boxes.count > 0U &&
                   static_cast<std::uint32_t>(now - last_box_update) >=
                       kBoxLifetimeMs) {
            active_boxes = EmptyBoxes();
            tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
                "lcd: stale inference boxes cleared\n")));
        }
        uai::sample2::memory_manager::CaptureFrame capture{};
        status = imager.TakeCompletedCapture(&capture);
        if (!status.Ok()) {
            if (status.code != ErrorCode::kNoFrame) {
                LogStatus("camera", status);
            }
            tk_dly_tsk(1);
            continue;
        }

        if (capture.sequence <= 3U ||
            (capture.sequence % 10U) == 0U) {
            tm_printf(reinterpret_cast<const UB *>(
                          "camera: frame captured sequence=%u buffer=%x event=%u aton_irq=%u last=%x\n"),
                      static_cast<unsigned int>(capture.sequence),
                      static_cast<unsigned int>(capture.buffer.address),
                      g_camera_frame_event_count, g_aton_irq_count,
                      g_aton_last_irqs);
        }
        LogFrameBrightness(capture);

        /* The ref project commits the display buffer on every completed
         * camera frame.  Keep the same behavior so LCD refresh does not
         * depend on the tick source while the HAL is being serviced. */
        const bool display_due = true;
        const bool inference_due = kCopyInferenceFrames &&
                                    (kInferenceMode == InferenceMode::kCopyOnly ||
                                     g_external_nor_ready) &&
                                    (first_frame ||
                                    static_cast<std::int32_t>(now -
                                                              next_inference) >=
                                        0);
        if (display_due) {
            if (capture.sequence <= 3U ||
                (capture.sequence % 10U) == 0U) {
                tm_printf(reinterpret_cast<const UB *>(
                              "lcd: compose begin sequence=%u\n"),
                          static_cast<unsigned int>(capture.sequence));
            }
            status = lcd.ComposeAndPresent(capture, active_boxes);
            if (!status.Ok()) {
                LogStatus("lcd", status);
                if (!IsBestEffort(status.code)) {
                    Halt("sample2: lcd compose failed\n");
                }
            } else if (capture.sequence <= 3U ||
                       (capture.sequence % 10U) == 0U) {
                tm_printf(reinterpret_cast<const UB *>(
                              "lcd: frame presented sequence=%u buffer=%x\n"),
                          static_cast<unsigned int>(capture.sequence),
                          static_cast<unsigned int>(capture.buffer.address));
            }
            if (capture.sequence <= 3U ||
                (capture.sequence % 10U) == 0U) {
                tm_printf(reinterpret_cast<const UB *>(
                              "lcd: compose end sequence=%u aton_irq=%u last=%x\n"),
                          static_cast<unsigned int>(capture.sequence),
                          g_aton_irq_count, g_aton_last_irqs);
            }
        }

        if (inference_due) {
            tm_printf(reinterpret_cast<const UB *>(
                          "ai: snapshot begin sequence=%u\n"),
                      static_cast<unsigned int>(capture.sequence));
            InferenceFrame inference{};
            status = g_memory.AcquireInferenceBuffer(capture, &inference);
            if (status.Ok()) {
                status = g_memory_hardware.PrepareForCpuRead(capture.buffer);
            }
            if (status.Ok()) {
                const std::uint32_t copy_start = Now();
                std::memcpy(
                    reinterpret_cast<void *>(inference.buffer.address),
                    reinterpret_cast<const void *>(capture.buffer.address),
                    uai::sample2::memory_manager::kFrameBytes);
                const std::uint32_t copy_elapsed = Now() - copy_start;
                const Error generation_after_copy =
                    g_memory.ValidateCaptureFrame(capture);
                if constexpr (kInferenceMode == InferenceMode::kCopyOnly) {
                    tm_printf(reinterpret_cast<const UB *>(
                                  "ai: copy-only sequence_before=%u bytes=%u elapsed_ms=%u generation_valid_after=%u code=%u\n"),
                              static_cast<unsigned int>(capture.sequence),
                              static_cast<unsigned int>(
                                  uai::sample2::memory_manager::kFrameBytes),
                              static_cast<unsigned int>(copy_elapsed),
                              static_cast<unsigned int>(
                                  generation_after_copy.Ok() ? 1U : 0U),
                              static_cast<unsigned int>(generation_after_copy.code));
                }
                status = {ErrorCode::kOk, capture.sequence,
                          "memory.snapshot_inference"};
            } else if (inference) {
                const Error release_status =
                    g_memory.ReleaseInferenceBuffer(inference);
                LogStatus("memory", release_status);
            }
            tm_printf(reinterpret_cast<const UB *>(
                          "ai: snapshot end sequence=%u code=%u detail=%x buffer=%x\n"),
                      static_cast<unsigned int>(capture.sequence),
                      static_cast<unsigned int>(status.code),
                      static_cast<unsigned int>(status.detail),
                      static_cast<unsigned int>(inference.buffer.address));
            if (status.Ok()) {
                if constexpr (kInferenceMode == InferenceMode::kNpu) {
                    SendInferenceFrame(inference);
                    tm_printf(reinterpret_cast<const UB *>(
                                  "ai: frame copied sequence=%u buffer=%x irq=%u last=%x\n"),
                              static_cast<unsigned int>(capture.sequence),
                              static_cast<unsigned int>(inference.buffer.address),
                              g_aton_irq_count, g_aton_last_irqs);
                } else {
                    status = g_memory.ReleaseInferenceBuffer(inference);
                    if (status.Ok()) {
                        tm_printf(reinterpret_cast<const UB *>(
                                      "ai: copy-only snapshot released sequence=%u\n"),
                                  static_cast<unsigned int>(capture.sequence));
                    } else {
                        LogStatus("memory", status);
                    }
                }
            } else if (!IsBestEffort(status.code)) {
                LogStatus("memory", status);
            }
            next_inference = now + kInferencePeriod;
        }
        first_frame = false;
        if ((loop_count % 1000U) == 0U) {
            tm_printf(reinterpret_cast<const UB *>(
                          "camera: heartbeat loop=%u sequence=%u aton_irq=%u last=%x\n"),
                      static_cast<unsigned int>(loop_count),
                      static_cast<unsigned int>(capture.sequence),
                      g_aton_irq_count, g_aton_last_irqs);
        }
        tk_dly_tsk(1);
    }
}

void inference_task(void)
{
    UINT pattern = 0U;
    const ER error = tk_wai_flg(g_external_memory_ready, kExternalMemoryReady,
                                TWF_ANDW, &pattern, TMO_FEVR);
    if (error != E_OK) {
        tm_printf(reinterpret_cast<const UB *>(
                      "error: component=ai operation=wait_memory code=%x detail=0\n"),
                  static_cast<unsigned int>(error));
        return;
    }

    uai::sample2::ModelManager model;
    const Error model_status =
        g_external_nor_ready
            ? model.Initialize(g_memory, g_memory_hardware)
            : Error{ErrorCode::kNotInitialized, 0U,
                    "ai.external_nor_unavailable"};
    if (model_status.Ok()) {
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "ai: model loaded; inference execution enabled\n")));
    } else {
        LogStatus("ai", model_status);
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "ai: model load failed; inference disabled\n")));
    }

    for (;;) {
        InferenceMessage message{};
        const INT size = tk_rcv_mbf(g_frame_queue, &message, TMO_FEVR);
        if (size != static_cast<INT>(sizeof(message))) {
            continue;
        }

        Error status = g_memory.ClaimInferenceBuffer(message.frame);
        if (!status.Ok()) {
            LogStatus("memory", status);
            continue;
        }

        BoxSet boxes{};
        if (model_status.Ok()) {
            tm_printf(reinterpret_cast<const UB *>(
                          "ai: inference begin sequence=%u\n"),
                      static_cast<unsigned int>(message.frame.capture_sequence));
            tm_printf(reinterpret_cast<const UB *>(
                          "ai: inference run begin sequence=%u input=%x irq=%u last=%x\n"),
                      static_cast<unsigned int>(message.frame.capture_sequence),
                      static_cast<unsigned int>(message.frame.buffer.address),
                      g_aton_irq_count, g_aton_last_irqs);
            status = model.TryInfer(message.frame, &boxes);
            tm_printf(reinterpret_cast<const UB *>(
                          "ai: inference run end sequence=%u code=%u detail=%x irq=%u last=%x\n"),
                      static_cast<unsigned int>(message.frame.capture_sequence),
                      static_cast<unsigned int>(status.code),
                      static_cast<unsigned int>(status.detail),
                      g_aton_irq_count, g_aton_last_irqs);
        } else {
            status = {ErrorCode::kNotInitialized, 0U, "ai.infer_disabled"};
        }
        const Error release_status =
            g_memory.ReleaseInferenceBuffer(message.frame);
        LogStatus("memory", release_status);
        if (status.Ok()) {
            SendLatestBoxes(boxes);
            tm_printf(reinterpret_cast<const UB *>(
                          "ai: inference sequence=%u capture=%u count=%u\n"),
                      static_cast<unsigned int>(boxes.model_sequence),
                      static_cast<unsigned int>(boxes.capture_sequence),
                      static_cast<unsigned int>(boxes.count));
        } else if (model_status.Ok()) {
            LogStatus("ai", status);
            LogNpuStatus(model.LastNpuStatus());
        }
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
        Halt("sample2: task create failed\n");
    }
    const ER error = tk_sta_tsk(task_id, 0);
    if (error != E_OK) {
        tm_printf(reinterpret_cast<const UB *>(
                      "error: component=main operation=start_%s code=%x detail=0\n"),
                  name, static_cast<unsigned int>(error));
        Halt("sample2: task start failed\n");
    }
}

} // namespace

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

/* µT-Kernelから呼び出されるsample2のエントリーポイント。 */
extern "C" INT usermain(void)
{
    DumpCoreRegisters("usermain");

    /* µT-Kernel replaces the startup vector table with its RAM table.  Keep
     * the Neural-ART and RIF handlers as direct IRQ handlers in that table;
     * they already have the Cortex-M exception entry/return ABI and must not
     * be called through the TA_HLNG wrapper (which adds an intno argument). */
    T_DINT npu_interrupt = {};
    npu_interrupt.intatr = TA_ASM;
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
        Halt("sample2: interrupt registration failed\n");
    }
    DumpCoreRegisters("after_interrupts");

    T_CFLG flag = {};
    flag.flgatr = TA_TFIFO | TA_WSGL;
    g_external_memory_ready = tk_cre_flg(&flag);
    if (g_external_memory_ready < E_OK) {
        Halt("sample2: event flag create failed\n");
    }

    T_CMBF frame_queue = {};
    frame_queue.mbfatr = TA_TFIFO;
    frame_queue.bufsz = sizeof(g_frame_queue_storage);
    frame_queue.maxmsz = sizeof(InferenceMessage);
    frame_queue.bufptr = g_frame_queue_storage;
    g_frame_queue = tk_cre_mbf(&frame_queue);
    if (g_frame_queue < E_OK) {
        Halt("sample2: frame queue create failed\n");
    }

    T_CMBF box_queue = {};
    box_queue.mbfatr = TA_TFIFO;
    box_queue.bufsz = sizeof(g_box_queue_storage);
    box_queue.maxmsz = sizeof(BoxMessage);
    box_queue.bufptr = g_box_queue_storage;
    g_box_queue = tk_cre_mbf(&box_queue);
    if (g_box_queue < E_OK) {
        Halt("sample2: box queue create failed\n");
    }

    StartTask(reinterpret_cast<FP>(application_initialize_task),
              g_initialization_task_stack, kInitializationTaskStackSize, 5,
              "application_initialize");

    for (;;) {
        tk_slp_tsk(TMO_FEVR);
    }
}
