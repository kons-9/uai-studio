#include "task/task_context.hpp"
#include "driver/npu_driver/registers/npu_registers.hpp"

namespace uai::ai::task {

void LogStatus(const char *component, const Error &error)
{
    if (!error.Ok()) {
        tm_printf(reinterpret_cast<const UB *>(
                      "error: component=%s operation=%s code=%s(%u) detail=%x\n"),
                  component, error.operation,
                  uai::ai::common::ErrorCodeName(error.code),
                  static_cast<unsigned int>(error.code),
                  static_cast<unsigned int>(error.detail));
    }
}

void LogFrameBrightness(
    const uai::ai::memory_allocator::CaptureFrame &capture)
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
        g_cache.PrepareForCpuRead(capture.buffer);
    if (!cache_status.Ok()) {
        LogStatus("camera-luminance", cache_status);
        return;
    }

    const auto *pixels =
        reinterpret_cast<const std::uint16_t *>(capture.buffer.address);
    constexpr std::size_t kPixelCount =
        uai::ai::memory_allocator::kFrameBytes / sizeof(*pixels);
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
    const int32_t sensor_register_status = AiReadSensorRegisters(
        &sensor_vmax, &sensor_shutter, &sensor_gain);
    tm_printf(reinterpret_cast<const UB *>(
                  "camera: image brightness seq=%u mean=%u peak=%u exposure_us=%u exposure_lines=%u gain_mdB=%d regs=%d/%u,%u,%u\n"),
              static_cast<unsigned int>(capture.sequence),
              static_cast<unsigned int>(luminance_sum / sample_count),
              static_cast<unsigned int>(luminance_peak),
              g_ai_last_exposure_request_us,
              g_ai_last_exposure_lines,
              static_cast<int>(AiGetSensorGainMdB()),
              static_cast<int>(sensor_register_status),
              static_cast<unsigned int>(sensor_vmax),
              static_cast<unsigned int>(sensor_shutter),
              static_cast<unsigned int>(sensor_gain));
}

void LogInferenceInput(
    const uai::ai::memory_allocator::InferenceFrame &frame)
{
    if (!frame ||
#if !defined(AI_MODEL_FACE)
        !frame.from_pipe2 ||
#endif
        frame.buffer.size < uai::ai::memory_allocator::kInferenceFrameBytes) {
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "ai: input inspect invalid frame\n")));
        return;
    }

    const uai::ai::memory_allocator::Buffer input_buffer{
        frame.buffer.address, uai::ai::memory_allocator::kInferenceFrameBytes,
        frame.buffer.index, uai::ai::memory_allocator::Region::kInference};
    const Error cache_status = frame.from_pipe2
                                   ? g_cache.PrepareForCpuRead(input_buffer)
                                   : g_cache.PrepareForPeripheralRead(input_buffer);
    if (!cache_status.Ok()) {
        LogStatus("ai-input", cache_status);
        return;
    }

    const auto *bytes = reinterpret_cast<const std::uint8_t *>(
        frame.buffer.address);
    const std::size_t byte_count =
        uai::ai::memory_allocator::kInferenceFrameBytes;
    std::uint32_t crc = 0xFFFFFFFFU;
    std::uint8_t minimum = 0xFFU;
    std::uint8_t maximum = 0U;
    for (std::size_t i = 0U; i < byte_count; ++i) {
        crc ^= bytes[i];
        for (std::uint32_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^
                  ((crc & 1U) != 0U ? 0xEDB88320U : 0U);
        }
        minimum = bytes[i] < minimum ? bytes[i] : minimum;
        maximum = bytes[i] > maximum ? bytes[i] : maximum;
    }
    crc = ~crc;

    constexpr std::size_t kPixelCount =
        uai::ai::memory_allocator::kInferenceWidth *
        uai::ai::memory_allocator::kInferenceHeight;
    constexpr std::size_t kSampleStep = 64U;
    std::uint32_t luminance_sum = 0U;
    std::uint32_t sample_count = 0U;
    for (std::size_t pixel = 0U; pixel < kPixelCount;
         pixel += kSampleStep) {
        const std::size_t index = pixel * 3U;
        const std::uint32_t red = bytes[index];
        const std::uint32_t green = bytes[index + 1U];
        const std::uint32_t blue = bytes[index + 2U];
        luminance_sum += (77U * red + 150U * green + 29U * blue) >> 8U;
        ++sample_count;
    }

    const std::size_t center =
        ((uai::ai::memory_allocator::kInferenceHeight / 2U) *
             uai::ai::memory_allocator::kInferenceWidth +
         uai::ai::memory_allocator::kInferenceWidth / 2U) *
        3U;
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: input inspect seq=%u address=%x bytes=%u crc=%x "
                  "min=%u max=%u mean_luma=%u p00=%02x/%02x/%02x "
                  "pcenter=%02x/%02x/%02x plast=%02x/%02x/%02x\n"),
              static_cast<unsigned int>(frame.capture_sequence),
              static_cast<unsigned int>(frame.buffer.address),
              static_cast<unsigned int>(byte_count),
              static_cast<unsigned int>(crc),
              static_cast<unsigned int>(minimum),
              static_cast<unsigned int>(maximum),
              static_cast<unsigned int>(luminance_sum / sample_count),
              bytes[0U], bytes[1U], bytes[2U], bytes[center],
              bytes[center + 1U], bytes[center + 2U],
              bytes[byte_count - 3U], bytes[byte_count - 2U],
              bytes[byte_count - 1U]);
}

void LogNpuStatus(const NpuStatus &status)
{
    const auto &execution = status.execution;
    const auto npu_hardware =
        uai::ai::npu::registers::NpuRegisterLayer{}.ReadSnapshot();
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

    const auto npu_hardware =
        uai::ai::npu::registers::NpuRegisterLayer{}.ReadSnapshot();
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

void DumpFrozenCapture(const uai::ai::memory_allocator::CaptureFrame &frame)
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
        uai::ai::memory_allocator::kFrameHeight;
    std::uint32_t last_nonzero = 0U;
    std::uint32_t distinct_row_crcs = 0U;
    std::uint32_t row_crcs[uai::ai::memory_allocator::kFrameHeight] = {};
    for (std::uint32_t y = 0U;
         y < uai::ai::memory_allocator::kFrameHeight; ++y) {
        const auto *row = pixels +
                          y * uai::ai::memory_allocator::kFrameWidth;
        bool any_nonzero = false;
        for (std::uint32_t x = 0U;
         x < uai::ai::memory_allocator::kFrameWidth; ++x) {
            any_nonzero = any_nonzero || row[x] != 0U;
        }
        row_crcs[y] = Crc32Bytes(
            reinterpret_cast<const std::uint8_t *>(row),
                          uai::ai::memory_allocator::kFrameWidth * 2U);
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
         y < uai::ai::memory_allocator::kFrameHeight &&
         reported_nonzero < 24U;
         ++y) {
        const auto *row = pixels +
                          y * uai::ai::memory_allocator::kFrameWidth;
        bool any_nonzero = false;
        for (std::uint32_t x = 0U;
         x < uai::ai::memory_allocator::kFrameWidth; ++x) {
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
            pixels + y * uai::ai::memory_allocator::kFrameWidth);
        const std::uint32_t row_crc = Crc32Bytes(
            row_bytes, uai::ai::memory_allocator::kFrameWidth * 2U);
        tm_printf(reinterpret_cast<const UB *>(
                      "camera: frozen row y=%u crc=%x samples="),
                  y, row_crc);
        for (const std::uint32_t x : kColumns) {
            tm_printf(reinterpret_cast<const UB *>("%04x%s"),
                      static_cast<unsigned int>(
                          pixels[y * uai::ai::memory_allocator::kFrameWidth +
                                 x]),
                      x == kColumns[sizeof(kColumns) / sizeof(kColumns[0]) - 1U]
                          ? "\n"
                          : ",");
        }
    }
}

} // namespace uai::ai::task
