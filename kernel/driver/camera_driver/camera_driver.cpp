#include "driver/camera_driver/camera_driver.hpp"
#include "middleware/foundation/log.hpp"
#include "driver/config/ai_board_config.hpp"
#include "image_resizer/image_resizer.hpp"
#include "middleware/pipeline/image_format.hpp"
#include "middleware/memory/generated/memory_config.hpp"
#include "memory_manager/memory_sizes.hpp"
#include "middleware/memory/static_memory_layout.hpp"
#include "middleware/memory/generated/static_memory_layout/key.hpp"

#include "driver/camera_driver/sensor_driver/registers/imx335_registers.hpp"

#include <cstdint>
#include <cstring>

#include <tk/tkernel.h>

extern "C" {
#include "stm32n6570_discovery_camera.h"
#include "stm32n6xx_hal.h"

extern DCMIPP_HandleTypeDef hcamera_dcmipp;
extern ISP_HandleTypeDef hcamera_isp;
ISP_StatusTypeDef AiSetImx335Exposure(uint32_t instance, int32_t exposure);
ISP_StatusTypeDef AiGetImx335Exposure(uint32_t instance, int32_t *exposure);
ISP_StatusTypeDef AiSetImx335Gain(uint32_t instance, int32_t gain_mdB);
ISP_StatusTypeDef AiGetImx335Gain(uint32_t instance, int32_t *gain_mdB);
void AiResetImx335ControlState(void);
}

namespace {

using StaticMemoryKey = uai::ai::static_memory_layout::Key;

constexpr std::uint32_t kSensorWidth = 2592U;
constexpr std::uint32_t kSensorHeight = 1944U;
constexpr std::uint32_t kOutputWidth = uai::ai::pipeline::kCaptureFormat.width;
constexpr std::uint32_t kOutputHeight = uai::ai::pipeline::kCaptureFormat.height;
constexpr std::uint32_t kFrameTimeoutMs = 2000U;
constexpr std::uint32_t kRecoveryRetryMs = 5000U;
constexpr UINT kPipe2FrameReadyEvent = 0x01U;
/* Start the sensor from a usable exposure instead of the AEC minimum. The
 * ISP will still converge from this seed on the first statistics updates. */
constexpr std::int32_t kStartupExposureMicroseconds = 23814;
constexpr std::int32_t kStartupGainMilliDb = 20000;
/* Match ref/ on the STM32N6570-DK.  Keep the CSI PHY configuration
 * unchanged; this only reduces the sensor frame rate to give the CSI link
 * the same timing margin as the reference application. */
constexpr std::int32_t kFrameRateFps = 20;

constexpr std::uint32_t kRawDumpWordLimit = 0x7F000U;
constexpr std::uint8_t kRawDumpSentinel = 0xA5U;
bool g_raw_dump_started = false;
bool g_raw_dump_reported = false;
std::uint32_t g_raw_dump_start_tick = 0U;

volatile std::uintptr_t g_completed_frame = 0U;
std::uintptr_t g_active_frame = 0U;
std::uintptr_t g_next_frame = 0U;
std::uintptr_t g_frame_buffer0 = 0U;
std::uintptr_t g_frame_buffer1 = 0U;
volatile std::uintptr_t g_completed_inference = 0U;
volatile std::uint32_t g_completed_inference_sequence = 0U;
std::uintptr_t g_active_inference = 0U;
std::uintptr_t g_next_inference = 0U;
std::uintptr_t g_inference_buffers[
    uai::ai::memory_manager::kInferenceBufferCount]{};
std::uintptr_t g_inference_drop_buffer = 0U;
/* Pipe2 writes the aspect-preserving image into the vertical center of the
 * square NPU tensor. Keep the allocator-visible address at the beginning of
 * the slot; only the DMA destination is offset. */
std::uintptr_t g_inference_dma_offset = 0U;
volatile std::uint32_t g_inference_sequence = 0U;
uai::ai::memory_manager::MemoryManager *g_pipe2_memory = nullptr;
ID g_pipe2_frame_event_flag = 0;
volatile std::uint32_t g_last_frame_tick = 0U;
volatile std::uint32_t g_last_csi_error_tick = 0U;
volatile bool g_csi_fault_pending = false;
std::uint32_t g_last_vsync_count = 0U;
std::uint32_t g_last_recovery_tick = 0U;
bool g_camera_recovery_attempted = false;

std::uintptr_t InferenceDmaAddress(std::uintptr_t buffer)
{
    return buffer + g_inference_dma_offset;
}

void ClearInferenceInput(std::uintptr_t buffer)
{
    std::memset(reinterpret_cast<void *>(buffer), 0U,
                uai::ai::memory_manager::kInferenceFrameBytes);
}

uai::ai::common::Error ApplyDcmippDecimation(std::uint32_t pipe,
                            const uai::ai::image_resizer::Selection &selection)
{
    if (selection.hardware != uai::ai::image_resizer::Hardware::kDcmipp) {
        return {uai::ai::common::ErrorCode::kHardware};
    }

    if (selection.dcmipp_decimation == 1U) {
        return HAL_DCMIPP_PIPE_DisableDecimation(&hcamera_dcmipp, pipe) ==
                       HAL_OK
                   ? uai::ai::common::Error{uai::ai::common::ErrorCode::kOk}
                   : uai::ai::common::Error{uai::ai::common::ErrorCode::kHardware};
    }

    DCMIPP_DecimationConfTypeDef decimation{};
    switch (selection.dcmipp_decimation) {
    case 2U:
        decimation.HRatio = DCMIPP_HDEC_1_OUT_2;
        decimation.VRatio = DCMIPP_VDEC_1_OUT_2;
        break;
    case 4U:
        decimation.HRatio = DCMIPP_HDEC_1_OUT_4;
        decimation.VRatio = DCMIPP_VDEC_1_OUT_4;
        break;
    case 8U:
        decimation.HRatio = DCMIPP_HDEC_1_OUT_8;
        decimation.VRatio = DCMIPP_VDEC_1_OUT_8;
        break;
    default:
        return {uai::ai::common::ErrorCode::kHardware};
    }
    if (HAL_DCMIPP_PIPE_SetDecimationConfig(&hcamera_dcmipp, pipe,
                                            &decimation) != HAL_OK ||
        HAL_DCMIPP_PIPE_EnableDecimation(&hcamera_dcmipp, pipe) != HAL_OK) {
        return {uai::ai::common::ErrorCode::kHardware};
    }
    return {uai::ai::common::ErrorCode::kOk};
}

uai::ai::common::Error SelectDcmippResize(std::uint32_t input_width,
                         std::uint32_t input_height,
                         std::uint32_t output_width,
                         std::uint32_t output_height,
                         uai::ai::image_resizer::Selection *selection)
{
    uai::ai::image_resizer::Request request{};
    request.input = uai::ai::image_resizer::InputKind::kCameraPipe;
    request.input_width = input_width;
    request.input_height = input_height;
    request.output_width = output_width;
    request.output_height = output_height;
    return uai::ai::image_resizer::Select(request, selection);
}

uai::ai::common::Error ConfigureSensor(uai::ai::camera::sensor::registers::Imx335RegisterLayer &registers)
{
    return registers.Configure(uai::ai::config::kCamera.imx335_test_pattern_mode,
                               kFrameRateFps);
}

uai::ai::common::Error ConfigurePipe()
{
    const float ratio_width = static_cast<float>(kSensorWidth) / kOutputWidth;
    const float ratio_height = static_cast<float>(kSensorHeight) / kOutputHeight;
    const float ratio = ratio_width < ratio_height ? ratio_width : ratio_height;
    DCMIPP_CropConfTypeDef crop{};
    crop.HSize = uai::ai::config::kCamera.bypass_downsize ? kOutputWidth
        : static_cast<std::uint32_t>(kOutputWidth * ratio);
    crop.VSize = uai::ai::config::kCamera.bypass_downsize ? kOutputHeight
        : static_cast<std::uint32_t>(kOutputHeight * ratio);
    crop.HStart = ((kSensorWidth - crop.HSize) / 2U) & ~1U;
    crop.VStart = ((kSensorHeight - crop.VSize) / 2U) & ~1U;
    crop.PipeArea = DCMIPP_POSITIVE_AREA;
    uai::ai::image_resizer::Selection resize{};
    uai::ai::common::Error resize_status = SelectDcmippResize(
        crop.HSize, crop.VSize, kOutputWidth, kOutputHeight, &resize);
    if (!resize_status.Ok()) {
        return resize_status;
    }
    if (HAL_DCMIPP_PIPE_SetCropConfig(&hcamera_dcmipp, DCMIPP_PIPE1, &crop) != HAL_OK ||
        HAL_DCMIPP_PIPE_EnableCrop(&hcamera_dcmipp, DCMIPP_PIPE1) != HAL_OK ||
        HAL_DCMIPP_PIPE_DisableRedBlueSwap(&hcamera_dcmipp, DCMIPP_PIPE1) != HAL_OK ||
        HAL_DCMIPP_PIPE_DisableGammaConversion(&hcamera_dcmipp, DCMIPP_PIPE1) != HAL_OK) {
        return {uai::ai::common::ErrorCode::kHardware};
    }
    resize_status = ApplyDcmippDecimation(DCMIPP_PIPE1, resize);
    if (!resize_status.Ok()) {
        return resize_status;
    }
    DCMIPP_DownsizeTypeDef downsize{};
    downsize.HRatio = static_cast<std::uint32_t>(
        8192.0F * resize.dcmipp_input_width / kOutputWidth);
    downsize.VRatio = static_cast<std::uint32_t>(
        8192.0F * resize.dcmipp_input_height / kOutputHeight);
    downsize.HDivFactor = (1024U * 8192U - 1U) / downsize.HRatio;
    downsize.VDivFactor = (1024U * 8192U - 1U) / downsize.VRatio;
    downsize.HSize = kOutputWidth;
    downsize.VSize = kOutputHeight;
    HAL_StatusTypeDef downsize_status = HAL_OK;
    if (uai::ai::config::kCamera.bypass_downsize) {
        downsize_status = HAL_DCMIPP_PIPE_DisableDownsize(&hcamera_dcmipp, DCMIPP_PIPE1);
    } else {
        downsize_status = HAL_DCMIPP_PIPE_SetDownsizeConfig(&hcamera_dcmipp, DCMIPP_PIPE1, &downsize);
        if (downsize_status == HAL_OK) {
            downsize_status = HAL_DCMIPP_PIPE_EnableDownsize(&hcamera_dcmipp, DCMIPP_PIPE1);
        }
    }
    if (downsize_status != HAL_OK) return {uai::ai::common::ErrorCode::kHardware};
    DCMIPP_PipeConfTypeDef pipe{};
    pipe.FrameRate = DCMIPP_FRAME_RATE_ALL;
    pipe.PixelPipePitch = kOutputWidth * 2U;
    pipe.PixelPackerFormat = DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1;
    const HAL_StatusTypeDef status = HAL_DCMIPP_PIPE_SetConfig(&hcamera_dcmipp, DCMIPP_PIPE1, &pipe);
    if (status != HAL_OK) {
        return {uai::ai::common::ErrorCode::kHardware};
    }
    UAI_LOG_DEBUG("image_resizer: pipe=1 hw=%s decimation=%u input=%ux%u output=%ux%u\n",
              uai::ai::image_resizer::HardwareName(resize.hardware),
              static_cast<unsigned int>(resize.dcmipp_decimation),
              static_cast<unsigned int>(resize.dcmipp_input_width),
              static_cast<unsigned int>(resize.dcmipp_input_height),
              static_cast<unsigned int>(kOutputWidth),
              static_cast<unsigned int>(kOutputHeight));
    return {uai::ai::common::ErrorCode::kOk};
}

uai::ai::common::Error ConfigureInferencePipe(std::uint32_t inference_width,
                             std::uint32_t inference_height)
{
    /* Pipe2 is the ancillary NN output. It consumes the same RAW10 CSI
     * stream as Pipe1, then performs the crop/scale and RGB888 packing in
     * hardware. Configure the CSI selector once for Pipe1 in the BSP and
     * share it here; reprogramming Pipe2's CSI selector corrupts the shared
     * ancillary stream on this board. */
    if (HAL_DCMIPP_PIPE_CSI_EnableShare(&hcamera_dcmipp, DCMIPP_PIPE2) !=
        HAL_OK) {
        return {uai::ai::common::ErrorCode::kHardware};
    }

    DCMIPP_CropConfTypeDef crop{};
    /* Keep the complete Pipe1 camera crop. The model inputs are square, so
     * DCMIPP produces an aspect-preserving image with a shorter content height
     * and writes it into the center of the square tensor. This is letterbox,
     * not a center crop and not a non-uniform stretch. */
    const std::uint32_t content_height =
        (inference_width * kOutputHeight + kOutputWidth - 1U) / kOutputWidth;
    if (content_height == 0U || content_height > inference_height) {
        return {uai::ai::common::ErrorCode::kInvalidArgument};
    }
    g_inference_dma_offset = static_cast<std::uintptr_t>(
        (inference_height - content_height) / 2U) * inference_width * 3U;
    const float ratio_width = static_cast<float>(kSensorWidth) / kOutputWidth;
    const float ratio_height = static_cast<float>(kSensorHeight) / kOutputHeight;
    const float display_to_sensor = ratio_width < ratio_height
                                        ? ratio_width
                                        : ratio_height;
    if (uai::ai::config::kCamera.bypass_downsize) {
        crop.HSize = inference_width;
        crop.VSize = content_height;
    } else {
        crop.HSize = static_cast<std::uint32_t>(
            static_cast<float>(kOutputWidth) * display_to_sensor);
        crop.VSize = static_cast<std::uint32_t>(
            static_cast<float>(kOutputHeight) * display_to_sensor);
    }
    crop.HStart = ((kSensorWidth - crop.HSize) / 2U) & ~1U;
    crop.VStart = ((kSensorHeight - crop.VSize) / 2U) & ~1U;
    crop.PipeArea = DCMIPP_POSITIVE_AREA;
    if (HAL_DCMIPP_PIPE_SetCropConfig(&hcamera_dcmipp, DCMIPP_PIPE2, &crop) !=
            HAL_OK ||
        HAL_DCMIPP_PIPE_EnableCrop(&hcamera_dcmipp, DCMIPP_PIPE2) != HAL_OK) {
        return {uai::ai::common::ErrorCode::kHardware};
    }

    uai::ai::image_resizer::Selection resize{};
    uai::ai::common::Error resize_status = SelectDcmippResize(
        crop.HSize, crop.VSize, inference_width, content_height, &resize);
    if (!resize_status.Ok()) {
        return resize_status;
    }
    resize_status = ApplyDcmippDecimation(DCMIPP_PIPE2, resize);
    if (!resize_status.Ok()) {
        return resize_status;
    }

    DCMIPP_DownsizeTypeDef downsize{};
    downsize.HRatio = static_cast<std::uint32_t>(
        8192.0F * resize.dcmipp_input_width / inference_width);
    downsize.VRatio = static_cast<std::uint32_t>(
        8192.0F * resize.dcmipp_input_height / content_height);
    downsize.HDivFactor = (1024U * 8192U - 1U) / downsize.HRatio;
    downsize.VDivFactor = (1024U * 8192U - 1U) / downsize.VRatio;
    downsize.HSize = inference_width;
    downsize.VSize = content_height;
    if (HAL_DCMIPP_PIPE_SetDownsizeConfig(&hcamera_dcmipp, DCMIPP_PIPE2,
                                          &downsize) != HAL_OK ||
        HAL_DCMIPP_PIPE_EnableDownsize(&hcamera_dcmipp, DCMIPP_PIPE2) !=
            HAL_OK) {
        return {uai::ai::common::ErrorCode::kHardware};
    }

    DCMIPP_PipeConfTypeDef pipe{};
    /* Keep this selectable: 1/4 reduces PSRAM traffic, while 1/2 or ALL can
     * make the bounding-box refresh visibly more responsive. */
    switch (uai::ai::config::kCamera.pipe2_frame_rate) {
    case uai::ai::config::PipeFrameRate::kAll:
        pipe.FrameRate = DCMIPP_FRAME_RATE_ALL;
        break;
    case uai::ai::config::PipeFrameRate::kOneOverTwo:
        pipe.FrameRate = DCMIPP_FRAME_RATE_1_OVER_2;
        break;
    case uai::ai::config::PipeFrameRate::kOneOverFour:
        pipe.FrameRate = DCMIPP_FRAME_RATE_1_OVER_4;
        break;
    }
    pipe.PixelPipePitch = inference_width * 3U;
    pipe.PixelPackerFormat = DCMIPP_PIXEL_PACKER_FORMAT_RGB888_YUV444_1;
    if (HAL_DCMIPP_PIPE_SetConfig(&hcamera_dcmipp, DCMIPP_PIPE2, &pipe) !=
            HAL_OK ||
        HAL_DCMIPP_PIPE_EnableRedBlueSwap(&hcamera_dcmipp, DCMIPP_PIPE2) !=
            HAL_OK ||
        HAL_DCMIPP_PIPE_DisableGammaConversion(&hcamera_dcmipp, DCMIPP_PIPE2) !=
            HAL_OK) {
        return {uai::ai::common::ErrorCode::kHardware};
    }
    UAI_LOG_DEBUG("camera: pipe2 input crop x=%u y=%u w=%u h=%u output=%ux%u\n",
              static_cast<unsigned int>(crop.HStart),
              static_cast<unsigned int>(crop.VStart),
              static_cast<unsigned int>(crop.HSize),
              static_cast<unsigned int>(crop.VSize),
              static_cast<unsigned int>(inference_width),
              static_cast<unsigned int>(content_height));
    UAI_LOG_DEBUG("image_resizer: pipe=2 hw=%s decimation=%u input=%ux%u output=%ux%u pad_top=%u model=%ux%u\n",
              uai::ai::image_resizer::HardwareName(resize.hardware),
              static_cast<unsigned int>(resize.dcmipp_decimation),
              static_cast<unsigned int>(resize.dcmipp_input_width),
              static_cast<unsigned int>(resize.dcmipp_input_height),
              static_cast<unsigned int>(inference_width),
              static_cast<unsigned int>(content_height),
              static_cast<unsigned int>((inference_height - content_height) / 2U),
              static_cast<unsigned int>(inference_width),
              static_cast<unsigned int>(inference_height));
    return {uai::ai::common::ErrorCode::kOk};
}

uai::ai::common::Error ConfigureRawDumpPipe()
{
    if (!uai::ai::config::kCamera.raw_dump) {
        return {uai::ai::common::ErrorCode::kOk};
    }
    DCMIPP_CSI_PIPE_ConfTypeDef csi_pipe{};
    csi_pipe.DataTypeMode = DCMIPP_DTMODE_DTIDA;
    csi_pipe.DataTypeIDA = DCMIPP_DT_RAW10;
    csi_pipe.DataTypeIDB = DCMIPP_DT_RAW10;
    if (HAL_DCMIPP_CSI_PIPE_SetConfig(&hcamera_dcmipp, DCMIPP_PIPE0, &csi_pipe) != HAL_OK) {
        return {uai::ai::common::ErrorCode::kHardware};
    }
    DCMIPP_PipeConfTypeDef pipe{};
    pipe.FrameRate = DCMIPP_FRAME_RATE_ALL;
    if (HAL_DCMIPP_PIPE_SetConfig(&hcamera_dcmipp, DCMIPP_PIPE0, &pipe) != HAL_OK) {
        return {uai::ai::common::ErrorCode::kHardware};
    }
    DCMIPP_CropConfTypeDef crop{};
    crop.HStart = (((kSensorWidth - kOutputWidth) / 2U) * 10U) / 32U;
    crop.HSize = (kOutputWidth * 10U) / 32U;
    crop.VStart = ((kSensorHeight - kOutputHeight) / 2U) & ~1U;
    crop.VSize = kOutputHeight;
    crop.PipeArea = DCMIPP_POSITIVE_AREA;
    if (HAL_DCMIPP_PIPE_SetCropConfig(&hcamera_dcmipp, DCMIPP_PIPE0, &crop) != HAL_OK ||
        HAL_DCMIPP_PIPE_EnableCrop(&hcamera_dcmipp, DCMIPP_PIPE0) != HAL_OK) {
        return {uai::ai::common::ErrorCode::kHardware};
    }
    WRITE_REG(hcamera_dcmipp.Instance->P0DCLMTR,
              DCMIPP_P0DCLMTR_ENABLE | kRawDumpWordLimit);
    return {uai::ai::common::ErrorCode::kOk};
}

void PrepareRawDump()
{
    if (!uai::ai::config::kCamera.raw_dump) {
        return;
    }
    g_raw_dump_started = false;
    g_raw_dump_reported = false;
    const auto &raw_dump =
        uai::ai::static_memory_layout::Region::GetRegionFromKey(
        StaticMemoryKey::kRawDump);
    std::memset(reinterpret_cast<void *>(raw_dump.address()), kRawDumpSentinel,
                raw_dump.size());
    SCB_CleanInvalidateDCache_by_Addr(
        reinterpret_cast<std::uint32_t *>(raw_dump.address()),
        static_cast<std::int32_t>(raw_dump.size()));
}

void StartRawDump()
{
    if (!uai::ai::config::kCamera.raw_dump) {
        return;
    }
    __HAL_DCMIPP_CLEAR_FLAG(&hcamera_dcmipp, DCMIPP_FLAG_PIPE0_FRAME |
                                                 DCMIPP_FLAG_PIPE0_VSYNC |
                                                 DCMIPP_FLAG_PIPE0_OVR |
                                                 DCMIPP_FLAG_PIPE0_LIMIT);
    if (HAL_DCMIPP_CSI_PIPE_Start(&hcamera_dcmipp, DCMIPP_PIPE0,
                                  DCMIPP_VIRTUAL_CHANNEL0,
                                  static_cast<std::uint32_t>(
                                      uai::ai::static_memory_layout::Region::GetRegionFromKey(
                                          StaticMemoryKey::kRawDump)
                                          .address()),
                                  DCMIPP_MODE_SNAPSHOT) != HAL_OK) {
        g_raw_dump_reported = true;
        return;
    }
    __HAL_DCMIPP_DISABLE_IT(&hcamera_dcmipp, DCMIPP_IT_PIPE0_FRAME |
                                                   DCMIPP_IT_PIPE0_VSYNC |
                                                   DCMIPP_IT_PIPE0_OVR);
    g_raw_dump_started = true;
    g_raw_dump_start_tick = HAL_GetTick();
}

void ProcessRawDump()
{
    if (!uai::ai::config::kCamera.raw_dump) {
        return;
    }
    if (!g_raw_dump_started || g_raw_dump_reported) return;
    const std::uint32_t flags = READ_REG(hcamera_dcmipp.Instance->CMSR2);
    if ((flags & DCMIPP_FLAG_PIPE0_FRAME) != 0U) {
        __HAL_DCMIPP_CLEAR_FLAG(&hcamera_dcmipp, DCMIPP_FLAG_PIPE0_FRAME);
        g_raw_dump_reported = true;
    } else if (HAL_GetTick() - g_raw_dump_start_tick >= 1000U) {
        g_raw_dump_reported = true;
    }
}

uai::ai::common::Error ApplyDemosaicDiagnostic()
{
    if (uai::ai::config::kCamera.demosaic_linear) {
        DCMIPP_RawBayer2RGBConfTypeDef demosaic_config{};
        demosaic_config.RawBayerType = DCMIPP_RAWBAYER_RGGB;
        HAL_StatusTypeDef status = HAL_DCMIPP_PIPE_SetISPRawBayer2RGBConfig(
            &hcamera_dcmipp, DCMIPP_PIPE1, &demosaic_config);
        if (status == HAL_OK) {
            status = HAL_DCMIPP_PIPE_EnableISPRawBayer2RGB(
                &hcamera_dcmipp, DCMIPP_PIPE1);
        }
        return status == HAL_OK
                   ? uai::ai::common::Error{uai::ai::common::ErrorCode::kOk}
                   : uai::ai::common::Error{uai::ai::common::ErrorCode::kHardware};
    }
    if (uai::ai::config::kCamera.disable_demosaic) {
        const HAL_StatusTypeDef status =
            HAL_DCMIPP_PIPE_DisableISPRawBayer2RGB(&hcamera_dcmipp,
                                                   DCMIPP_PIPE1);
        return status == HAL_OK
                   ? uai::ai::common::Error{uai::ai::common::ErrorCode::kOk}
                   : uai::ai::common::Error{uai::ai::common::ErrorCode::kHardware};
    }
    return {uai::ai::common::ErrorCode::kOk};
}

uai::ai::common::Error StartStream(const uai::ai::camera::sensor::registers::Imx335RegisterLayer &registers)
{
    return registers.SetStreaming(true);
}

} // namespace

extern "C" {
volatile unsigned int g_camera_vsync_event_count = 0U;
volatile unsigned int g_camera_frame_event_count = 0U;
volatile unsigned int g_camera_recovery_count = 0U;
volatile unsigned int g_camera_recovery_error_count = 0U;
volatile unsigned int g_camera_isp_error_count = 0U;
volatile unsigned int g_camera_dcmipp_last_status = 0U;
volatile unsigned int g_camera_dcmipp_error_count = 0U;
volatile unsigned int g_camera_camera_error_count = 0U;
volatile unsigned int g_camera_pipe2_frame_event_count = 0U;
volatile unsigned int g_camera_pipe2_drop_count = 0U;
volatile unsigned int g_camera_csi_last_status = 0U;
volatile unsigned int g_camera_csi_last_status1 = 0U;
volatile unsigned int g_camera_csi_last_pending_status = 0U;
volatile unsigned int g_camera_csi_last_pending_status1 = 0U;
volatile unsigned int g_camera_csi_error_count = 0U;
volatile unsigned int g_camera_csi_last_error_code = 0U;
volatile unsigned int g_camera_csi_sot_sync_dl0_count = 0U;
volatile unsigned int g_camera_csi_sot_sync_dl1_count = 0U;
volatile unsigned int g_camera_csi_sot_dl0_count = 0U;
volatile unsigned int g_camera_csi_sot_dl1_count = 0U;
}

namespace uai::ai::camera {

Diagnostics::Diagnostics()
    : vsync_event_count(g_camera_vsync_event_count),
      frame_event_count(g_camera_frame_event_count),
      recovery_count(g_camera_recovery_count),
      recovery_error_count(g_camera_recovery_error_count),
      isp_error_count(g_camera_isp_error_count),
      dcmipp_last_status(g_camera_dcmipp_last_status),
      dcmipp_error_count(g_camera_dcmipp_error_count),
      camera_error_count(g_camera_camera_error_count),
      pipe2_frame_event_count(g_camera_pipe2_frame_event_count),
      pipe2_drop_count(g_camera_pipe2_drop_count),
      pipe2_latest_capture_sequence(g_inference_sequence),
      csi_last_status(g_camera_csi_last_status),
      csi_last_status1(g_camera_csi_last_status1),
      csi_last_pending_status(g_camera_csi_last_pending_status),
      csi_last_pending_status1(g_camera_csi_last_pending_status1),
      csi_error_count(g_camera_csi_error_count),
      csi_last_error_code(g_camera_csi_last_error_code),
      csi_sot_sync_dl0_count(g_camera_csi_sot_sync_dl0_count),
      csi_sot_sync_dl1_count(g_camera_csi_sot_sync_dl1_count),
      csi_sot_dl0_count(g_camera_csi_sot_dl0_count),
      csi_sot_dl1_count(g_camera_csi_sot_dl1_count)
{}

Diagnostics CameraDriver::GetDiagnostics() const { return Diagnostics{}; }

namespace {

void LogCameraLinkState(
    const sensor::registers::Imx335RegisterLayer &sensor_registers)
{
    std::uint8_t mode = 0xFFU;
    const bool sensor_read_ok = sensor_registers.Read(
        sensor::registers::Imx335Register::kModeSelect, &mode,
        sizeof(mode)).Ok();
    const std::uint32_t dcmipp_status1 = hcamera_dcmipp.Instance->CMSR1;
    const std::uint32_t dcmipp_status2 = hcamera_dcmipp.Instance->CMSR2;
    const std::uint32_t pipe_status = hcamera_dcmipp.Instance->P1SR;

    UAI_LOG_DEBUG("camera: diag t=%u f=%u v=%u imx=%x/%u "
                  "csi=%x/%x dcmipp=%x/%x p1=%x\n",
              static_cast<unsigned int>(HAL_GetTick()),
              g_camera_frame_event_count, g_camera_vsync_event_count,
              static_cast<unsigned int>(mode),
              static_cast<unsigned int>(sensor_read_ok), CSI->SR0, CSI->SR1,
              dcmipp_status1, dcmipp_status2, pipe_status);
}

} // namespace

void InstallExposureWorkaround()
{
    hcamera_isp.appliHelpers.SetSensorExposure = AiSetImx335Exposure;
    hcamera_isp.appliHelpers.GetSensorExposure = AiGetImx335Exposure;
    hcamera_isp.appliHelpers.SetSensorGain = AiSetImx335Gain;
    hcamera_isp.appliHelpers.GetSensorGain = AiGetImx335Gain;
    AiResetImx335ControlState();

    const ISP_StatusTypeDef exposure_status = AiSetImx335Exposure(
        0U, kStartupExposureMicroseconds);
    const ISP_StatusTypeDef gain_status =
        AiSetImx335Gain(0U, kStartupGainMilliDb);
    if (exposure_status != ISP_OK || gain_status != ISP_OK) {
        UAI_LOG_WARN("camera: exposure seed failed exposure=%d gain=%d\n",
                     static_cast<int>(exposure_status),
                     static_cast<int>(gain_status));
    }
}

uai::ai::common::Error CameraDriver::Initialize(memory_manager::MemoryManager &memory,
                                cache::CacheManagement &cache)
{
    if (initialized_) return {uai::ai::common::ErrorCode::kAlreadyInitialized};
    uai::ai::common::Error management_status =
        management_->Initialize();
    if (!management_status.Ok() &&
        management_status.Code() != uai::ai::common::ErrorCode::kAlreadyInitialized) {
        return management_status;
    }
    Writer writer;
    management_status = management_->Acquire(&writer);
    if (!management_status.Ok()) return management_status;
    buffer::Buffer first{};
    buffer::Buffer second{};
    if (!memory.CaptureBuffer(0U, &first).Ok() ||
        !memory.CaptureBuffer(1U, &second).Ok()) {
        return {uai::ai::common::ErrorCode::kNotInitialized};
    }
    if (BSP_CAMERA_Init(0U, CAMERA_R2592x1944, CAMERA_PF_RAW_RGGB10) != BSP_ERROR_NONE) return {uai::ai::common::ErrorCode::kHardware};
    uai::ai::camera::sensor::registers::Imx335RegisterLayer registers;
    if (!ConfigureSensor(registers).Ok()) return {uai::ai::common::ErrorCode::kHardware};
    InstallExposureWorkaround();
    if (!ConfigurePipe().Ok() ||
        !ConfigureInferencePipe(
             pipeline::kInferenceFormat.width,
             pipeline::kInferenceFormat.height)
             .Ok() ||
        !ConfigureRawDumpPipe().Ok()) {
        return {uai::ai::common::ErrorCode::kHardware};
    }
    memory_ = &memory;
    cache_ = &cache;
    g_pipe2_memory = &memory;
    if (g_pipe2_frame_event_flag == 0) {
        T_CFLG event_flag = {};
        event_flag.flgatr = TA_TFIFO;
        g_pipe2_frame_event_flag = tk_cre_flg(&event_flag);
        if (g_pipe2_frame_event_flag < E_OK) {
            const ID event_status = g_pipe2_frame_event_flag;
            g_pipe2_frame_event_flag = 0;
            return {uai::ai::common::ErrorCode::kInvalidState};
        }
    }
    initialized_ = true;
    return {uai::ai::common::ErrorCode::kOk};
}

void CameraDriver::KeepClocksOnSleep() const
{
    Writer writer;
    if (!management_->Acquire(&writer).Ok()) return;
    KeepClocksOnSleep(writer);
}

void CameraDriver::KeepClocksOnSleep(const Writer &writer) const
{
    if (!management_->Validate(writer).Ok()) return;
    __HAL_RCC_DCMIPP_CLK_SLEEP_ENABLE();
    __HAL_RCC_CSI_CLK_SLEEP_ENABLE();
}

uai::ai::common::Error CameraDriver::Start()
{
    Writer writer;
    uai::ai::common::Error status = management_->Acquire(&writer);
    if (!status.Ok()) return status;
    return Start(writer);
}

uai::ai::common::Error CameraDriver::Start(const Writer &writer)
{
    uai::ai::common::Error ownership =
        management_->Validate(writer);
    if (!ownership.Ok()) return ownership;
    if (!initialized_ || memory_ == nullptr || cache_ == nullptr) return {uai::ai::common::ErrorCode::kNotInitialized};
    if (started_) return {uai::ai::common::ErrorCode::kAlreadyInitialized};
    buffer::Buffer first_buffer{};
    buffer::Buffer second_buffer{};
    uai::ai::common::Error status =
        memory_->CaptureBuffer(0U, &first_buffer);
    if (!status.Ok()) return status;
    status = memory_->CaptureBuffer(1U, &second_buffer);
    if (!status.Ok()) return status;
    status = cache_->PrepareForDmaWrite(first_buffer);
    if (!status.Ok()) return status;
    status = cache_->PrepareForDmaWrite(second_buffer);
    if (!status.Ok()) return status;
    buffer::Buffer inference_buffers[
        memory_manager::kInferenceBufferCount]{};
    for (std::size_t i = 0U; i < memory_manager::kInferenceBufferCount; ++i) {
        status = memory_->InferenceBuffer(static_cast<std::uint8_t>(i),
                                          &inference_buffers[i]);
        if (!status.Ok()) return status;
        ClearInferenceInput(inference_buffers[i].address);
        status = cache_->PrepareForDmaWrite(inference_buffers[i]);
        if (!status.Ok()) return status;
        g_inference_buffers[i] = inference_buffers[i].address;
    }
    g_frame_buffer0 = first_buffer.address;
    g_frame_buffer1 = second_buffer.address;
    g_active_frame = g_frame_buffer0;
    g_next_frame = g_frame_buffer1;
    g_completed_frame = 0U;
    buffer::Buffer inference_drop_buffer{};
    status = memory_->InferenceDropBuffer(&inference_drop_buffer);
    if (!status.Ok()) return status;
    g_inference_drop_buffer = inference_drop_buffer.address;
    status = cache_->PrepareForDmaWrite(inference_drop_buffer);
    if (!status.Ok()) return status;
    g_active_inference = g_inference_buffers[0];
    g_next_inference = g_inference_buffers[1];
    g_completed_inference = 0U;
    g_inference_sequence = 0U;
    if (g_pipe2_frame_event_flag > 0) {
        (void)tk_clr_flg(g_pipe2_frame_event_flag, 0U);
    }
    g_csi_fault_pending = false;
    g_camera_recovery_attempted = false;
    PrepareRawDump();
    if (BSP_CAMERA_Start(0U, reinterpret_cast<uint8_t *>(g_active_frame), CAMERA_MODE_CONTINUOUS) != BSP_ERROR_NONE) return {uai::ai::common::ErrorCode::kHardware};
    if (HAL_DCMIPP_CSI_PIPE_Start(&hcamera_dcmipp, DCMIPP_PIPE2,
                                  DCMIPP_VIRTUAL_CHANNEL0,
                                  static_cast<std::uint32_t>(InferenceDmaAddress(
                                      g_active_inference)),
                                  DCMIPP_MODE_CONTINUOUS) != HAL_OK) {
        return {uai::ai::common::ErrorCode::kHardware};
    }
    UAI_LOG_INFO("camera: pipe1=started pipe2=started\n");
    status = ApplyDemosaicDiagnostic();
    if (!status.Ok()) return status;
    uai::ai::camera::sensor::registers::Imx335RegisterLayer registers;
    status = StartStream(registers);
    if (!status.Ok()) return status;
    StartRawDump();
    g_last_frame_tick = HAL_GetTick();
    g_last_vsync_count = g_camera_vsync_event_count;
    started_ = true;
    LogCameraLinkState(registers);
    return {uai::ai::common::ErrorCode::kOk};
}

uai::ai::common::Error CameraDriver::Stop()
{
    Writer writer;
    uai::ai::common::Error status = management_->Acquire(&writer);
    if (!status.Ok()) return status;
    return Stop(writer);
}

uai::ai::common::Error CameraDriver::Stop(const Writer &writer)
{
    uai::ai::common::Error ownership =
        management_->Validate(writer);
    if (!ownership.Ok()) return ownership;
    if (!initialized_ || !started_) return {uai::ai::common::ErrorCode::kNotInitialized};
    uai::ai::camera::sensor::registers::Imx335RegisterLayer registers;
    const auto standby = registers.SetStreaming(false);
    if (!standby.Ok()) return standby;
    if (HAL_DCMIPP_CSI_PIPE_Stop(&hcamera_dcmipp, DCMIPP_PIPE2,
                                 DCMIPP_VIRTUAL_CHANNEL0) != HAL_OK) {
        return {uai::ai::common::ErrorCode::kHardware};
    }
    if (BSP_CAMERA_Stop(0U) != BSP_ERROR_NONE) return {uai::ai::common::ErrorCode::kHardware};
    started_ = false;
    return {uai::ai::common::ErrorCode::kOk};
}

uai::ai::common::Error CameraDriver::Process()
{
    Writer writer;
    uai::ai::common::Error status = management_->Acquire(&writer);
    if (!status.Ok()) return status;
    return Process(writer);
}

uai::ai::common::Error CameraDriver::Process(const Writer &writer)
{
    uai::ai::common::Error ownership =
        management_->Validate(writer);
    if (!ownership.Ok()) return ownership;
    if (!initialized_ || !started_) return {uai::ai::common::ErrorCode::kNotInitialized};
    ProcessRawDump();
    if (g_camera_vsync_event_count != g_last_vsync_count) {
        g_last_vsync_count = g_camera_vsync_event_count;
        if (BSP_CAMERA_BackgroundProcess() != BSP_ERROR_NONE) ++g_camera_isp_error_count;
    }
    const std::uint32_t now = HAL_GetTick();
    const bool timed_out = now - g_last_frame_tick >= kFrameTimeoutMs;
    /* A CSI error is diagnostic information, not by itself a reason to
     * restart the sensor.  The reference application waits for an actual
     * frame timeout; doing the same avoids restarting on a transient SOT or
     * D-PHY status bit while frames are still arriving. */
    if (timed_out &&
        (!g_camera_recovery_attempted ||
         now - g_last_recovery_tick >= kRecoveryRetryMs)) {
        UAI_LOG_WARN("camera: no frame for %u ms; starting recovery #%u\n",
                     static_cast<unsigned int>(now - g_last_frame_tick),
                     g_camera_recovery_count + 1U);
        uai::ai::camera::sensor::registers::Imx335RegisterLayer registers;
        LogCameraLinkState(registers);
        g_last_recovery_tick = now;
        g_camera_recovery_attempted = true;
        ++g_camera_recovery_count;
        g_completed_frame = 0U;
        g_active_frame = g_frame_buffer0;
        g_next_frame = g_frame_buffer1;
        if (g_completed_inference != 0U && g_pipe2_memory != nullptr) {
            (void)g_pipe2_memory->DropCompletedInference(
                g_completed_inference, g_completed_inference_sequence);
        }
        g_completed_inference = 0U;
        g_completed_inference_sequence = 0U;
        g_active_inference = g_inference_buffers[0];
        g_next_inference = g_inference_buffers[1];
        if (g_pipe2_frame_event_flag > 0) {
            (void)tk_clr_flg(g_pipe2_frame_event_flag, 0U);
        }
        (void)registers.SetStreaming(false);
        HAL_Delay(20U);
        const HAL_StatusTypeDef pipe_stop_status =
            HAL_DCMIPP_CSI_PIPE_Stop(&hcamera_dcmipp, DCMIPP_PIPE1,
                                     DCMIPP_VIRTUAL_CHANNEL0);
        const HAL_StatusTypeDef pipe2_stop_status =
            HAL_DCMIPP_CSI_PIPE_Stop(&hcamera_dcmipp, DCMIPP_PIPE2,
                                     DCMIPP_VIRTUAL_CHANNEL0);
        bool recovery_ok = pipe_stop_status == HAL_OK &&
            pipe2_stop_status == HAL_OK &&
            HAL_DCMIPP_DeInit(&hcamera_dcmipp) == HAL_OK &&
            BSP_CAMERA_Init(0U, CAMERA_R2592x1944, CAMERA_PF_RAW_RGGB10) == BSP_ERROR_NONE &&
            ConfigureSensor(registers).Ok();
        if (recovery_ok) {
            InstallExposureWorkaround();
            recovery_ok = ConfigurePipe().Ok() &&
                          ConfigureInferencePipe(
                              pipeline::kInferenceFormat.width,
                              pipeline::kInferenceFormat.height)
                              .Ok() &&
                          ConfigureRawDumpPipe().Ok();
        }
        if (!recovery_ok ||
            BSP_CAMERA_Start(0U, reinterpret_cast<uint8_t *>(g_active_frame), CAMERA_MODE_CONTINUOUS) != BSP_ERROR_NONE ||
            HAL_DCMIPP_CSI_PIPE_Start(
                &hcamera_dcmipp, DCMIPP_PIPE2, DCMIPP_VIRTUAL_CHANNEL0,
                static_cast<std::uint32_t>(g_active_inference),
                DCMIPP_MODE_CONTINUOUS) != HAL_OK ||
            !ApplyDemosaicDiagnostic().Ok() || !StartStream(registers).Ok()) {
            ++g_camera_recovery_error_count;
            LogCameraLinkState(registers);
            UAI_LOG_ERROR("camera: recovery failed attempts=%u failed=%u\n",
                          g_camera_recovery_count, g_camera_recovery_error_count);
        } else {
            InstallExposureWorkaround();
            PrepareRawDump();
            StartRawDump();
            g_csi_fault_pending = false;
            g_last_frame_tick = now;
            g_last_vsync_count = g_camera_vsync_event_count;
            LogCameraLinkState(registers);
            UAI_LOG_WARN("camera: recovery done attempts=%u frames=%u\n",
                         g_camera_recovery_count, g_camera_frame_event_count);
        }
    }
    return {uai::ai::common::ErrorCode::kOk};
}

uai::ai::common::Error CameraDriver::TakeCompletedCapture(pipeline::CaptureFrame *frame)
{
    Writer writer;
    uai::ai::common::Error status = management_->Acquire(&writer);
    if (!status.Ok()) return status;
    return TakeCompletedCapture(frame, writer);
}

uai::ai::common::Error CameraDriver::TakeCompletedCapture(
    pipeline::CaptureFrame *frame, const Writer &writer)
{
    uai::ai::common::Error ownership =
        management_->Validate(writer);
    if (!ownership.Ok()) return ownership;
    if (!initialized_ || memory_ == nullptr) return {uai::ai::common::ErrorCode::kNotInitialized};
    if (frame == nullptr) return {uai::ai::common::ErrorCode::kInvalidArgument};
    const std::uintptr_t address = g_completed_frame;
    g_completed_frame = 0U;
    if (address == 0U) return {uai::ai::common::ErrorCode::kNoFrame};
    return memory_->ImportCompletedCapture(address, frame);
}

uai::ai::common::Error CameraDriver::TakeCompletedInference(
    pipeline::InferenceFrame *frame)
{
    Writer writer;
    uai::ai::common::Error status = management_->Acquire(&writer);
    if (!status.Ok()) return status;
    return TakeCompletedInference(frame, writer);
}

uai::ai::common::Error CameraDriver::TakeCompletedInference(
    pipeline::InferenceFrame *frame, const Writer &writer)
{
    uai::ai::common::Error ownership =
        management_->Validate(writer);
    if (!ownership.Ok()) return ownership;
    if (!initialized_ || memory_ == nullptr) {
        return {uai::ai::common::ErrorCode::kNotInitialized};
    }
    if (frame == nullptr) {
        return {uai::ai::common::ErrorCode::kInvalidArgument};
    }
    if (g_pipe2_frame_event_flag > 0) {
        UINT pattern = 0U;
        const ER event_status = tk_wai_flg(
            g_pipe2_frame_event_flag, kPipe2FrameReadyEvent,
            TWF_ANDW | TWF_BITCLR, &pattern, TMO_POL);
        if (event_status != E_OK) {
            return {uai::ai::common::ErrorCode::kNoFrame};
        }
    }
    const std::uintptr_t address = g_completed_inference;
    const std::uint32_t sequence = g_completed_inference_sequence;
    g_completed_inference = 0U;
    g_completed_inference_sequence = 0U;
    if (address == 0U) {
        return {uai::ai::common::ErrorCode::kNoFrame};
    }
    return memory_->ImportCompletedInference(address, sequence, frame);
}

uai::ai::common::Error CameraDriver::SnapshotInferenceSource(
    pipeline::InferenceFrame *frame)
{
    Writer writer;
    uai::ai::common::Error status = management_->Acquire(&writer);
    if (!status.Ok()) return status;
    return SnapshotInferenceSource(frame, writer);
}

uai::ai::common::Error CameraDriver::SnapshotInferenceSource(
    pipeline::InferenceFrame *frame, const Writer &writer)
{
    uai::ai::common::Error ownership =
        management_->Validate(writer);
    if (!ownership.Ok()) return ownership;
    if (!initialized_ || memory_ == nullptr || cache_ == nullptr) {
        return {uai::ai::common::ErrorCode::kNotInitialized};
    }
    if (frame == nullptr || !frame->from_pipe2 || !frame->buffer ||
        !frame->source ||
        frame->source.size < memory_manager::kInferenceSourceBytes) {
        return {uai::ai::common::ErrorCode::kInvalidArgument};
    }

    const std::size_t content_bytes =
        memory_manager::kInferenceScratchBytes;
    const std::size_t pad_bytes =
        (memory_manager::kInferenceFrameBytes - content_bytes) /
        2U;
    if (g_inference_dma_offset != pad_bytes ||
        content_bytes + pad_bytes + pad_bytes > frame->source.size) {
        return {uai::ai::common::ErrorCode::kInvalidArgument};
    }
    const buffer::Buffer pipe2_content{
        frame->buffer.address + g_inference_dma_offset, content_bytes,
        frame->buffer.index, buffer::Region::kInference,
        memory_manager::kMemoryConfig.buffer_alignment};
    uai::ai::common::Error status =
        cache_->PrepareForCpuRead(pipe2_content);
    if (!status.Ok()) {
        return status;
    }

    auto *source = reinterpret_cast<std::uint8_t *>(frame->source.address);
    const auto *pipe2 = reinterpret_cast<const std::uint8_t *>(
                            frame->buffer.address) +
                        g_inference_dma_offset;
    std::memset(source, 0U, pad_bytes);
    std::memcpy(source + pad_bytes, pipe2, content_bytes);
    std::memset(source + pad_bytes + content_bytes, 0U, pad_bytes);

    status = cache_->PrepareForPeripheralRead(frame->source);
    if (!status.Ok()) {
        return status;
    }
    frame->source_valid = true;
    return {uai::ai::common::ErrorCode::kOk};
}

} // namespace uai::ai::camera

extern "C" void BSP_CAMERA_FrameEventCallback(uint32_t Instance)
{
    (void)Instance;
    ++g_camera_frame_event_count;
    if (HAL_DCMIPP_PIPE_SetMemoryAddress(&hcamera_dcmipp, DCMIPP_PIPE1, DCMIPP_MEMORY_ADDRESS_0, static_cast<uint32_t>(g_next_frame)) != HAL_OK) {
        ++g_camera_dcmipp_error_count;
        return;
    }
    g_last_frame_tick = HAL_GetTick();
    g_csi_fault_pending = false;
    g_camera_recovery_attempted = false;
    g_completed_frame = g_active_frame;
    g_active_frame = g_next_frame;
    g_next_frame = g_next_frame == g_frame_buffer0 ? g_frame_buffer1 : g_frame_buffer0;
}

extern "C" void BSP_CAMERA_VsyncEventCallback(uint32_t Instance) { (void)Instance; ++g_camera_vsync_event_count; }
extern "C" void BSP_CAMERA_PipeErrorCallback(uint32_t Instance) { (void)Instance; ++g_camera_dcmipp_error_count; }
extern "C" void BSP_CAMERA_ErrorCallback(uint32_t Instance) { (void)Instance; ++g_camera_camera_error_count; }

extern "C" void AiCameraPipe2FrameEventCallback(void)
{
    ++g_camera_pipe2_frame_event_count;
    /* This is the sequence of every Pipe2 completion, including frames sent
     * to the drop sink. It is intentionally separate from the sequence of a
     * frame handed to inference so lag can expose dropped intermediate frames. */
    const std::uint32_t capture_sequence = ++g_inference_sequence;
    const std::uintptr_t completed = g_active_inference;
    const bool completed_is_real_buffer =
        completed != 0U && completed != g_inference_drop_buffer;
    if (completed_is_real_buffer) {
        /* If the camera task has not consumed the previous completion yet,
         * release that stale result before replacing it.  Reserve the new
         * completion before selecting the next DMA target; otherwise a
         * delayed camera task may copy an image after DMA has overwritten it. */
        const std::uintptr_t previous = g_completed_inference;
        const std::uint32_t previous_sequence =
            g_completed_inference_sequence;
        if (previous != 0U && previous != completed &&
            g_pipe2_memory != nullptr) {
            (void)g_pipe2_memory->DropCompletedInference(
                previous, previous_sequence);
            g_completed_inference = 0U;
            g_completed_inference_sequence = 0U;
        }

        const uai::ai::common::Error reserve_status =
            g_pipe2_memory == nullptr
                ? uai::ai::common::Error{
                      uai::ai::common::ErrorCode::kNotInitialized}
                : g_pipe2_memory->ReserveCompletedInference(
                      completed, capture_sequence);
        if (reserve_status.Ok()) {
            g_completed_inference = completed;
            g_completed_inference_sequence = capture_sequence;
            __DMB();
            if (g_pipe2_frame_event_flag > 0) {
                (void)tk_set_flg(g_pipe2_frame_event_flag,
                                 kPipe2FrameReadyEvent);
            }
        }
    }

    std::uintptr_t selected = 0U;
    const std::uintptr_t candidates[] = {
        g_next_inference,
        g_inference_buffers[0],
        g_inference_buffers[1],
        g_inference_buffers[2],
    };
    for (const std::uintptr_t candidate : candidates) {
        if (candidate == 0U || candidate == completed ||
            candidate == g_inference_drop_buffer ||
            g_pipe2_memory == nullptr ||
            !g_pipe2_memory->IsInferenceBufferFree(candidate)) {
            continue;
        }
        selected = candidate;
        break;
    }
    if (selected == 0U) {
        /* All real buffers are owned by the camera/inference pipeline.  Keep
         * the producer running, but send the next frame to a DMA-only sink;
         * otherwise Pipe2 would overwrite the frame being preprocessed. */
        selected = g_inference_drop_buffer;
        ++g_camera_pipe2_drop_count;
    }
    if (HAL_DCMIPP_PIPE_SetMemoryAddress(
            &hcamera_dcmipp, DCMIPP_PIPE2, DCMIPP_MEMORY_ADDRESS_0,
            static_cast<std::uint32_t>(InferenceDmaAddress(selected))) != HAL_OK) {
        ++g_camera_dcmipp_error_count;
        if (completed_is_real_buffer && g_completed_inference != 0U &&
            g_pipe2_memory != nullptr) {
            (void)g_pipe2_memory->DropCompletedInference(
                g_completed_inference, g_completed_inference_sequence);
            g_completed_inference = 0U;
            g_completed_inference_sequence = 0U;
        }
        return;
    }
    g_active_inference = selected;
    g_next_inference = g_inference_buffers[0];
    for (std::size_t i = 0U;
         i < uai::ai::memory_manager::kInferenceBufferCount; ++i) {
        if (selected == g_inference_buffers[i]) {
            g_next_inference = g_inference_buffers[
                (i + 1U) % uai::ai::memory_manager::kInferenceBufferCount];
            break;
        }
    }
}

extern "C" void DCMIPP_IRQHandler(void)
{
    g_camera_dcmipp_last_status = hcamera_dcmipp.Instance->CMSR2;
    HAL_DCMIPP_IRQHandler(&hcamera_dcmipp);
}

extern "C" void CSI_IRQHandler(void)
{
    const uint32_t status0 = CSI->SR0, status1 = CSI->SR1;
    constexpr uint32_t errors0 =
        DCMIPP_CSI_IT_SYNCERR | DCMIPP_CSI_IT_WDERR |
        DCMIPP_CSI_IT_SPKTERR | DCMIPP_CSI_IT_IDERR |
        DCMIPP_CSI_IT_CECCERR | DCMIPP_CSI_IT_ECCERR |
        DCMIPP_CSI_IT_CRCERR | DCMIPP_CSI_IT_CCFIFO;
    constexpr uint32_t errors1 =
        DCMIPP_CSI_IT_ECTRLDL1 | DCMIPP_CSI_IT_ESYNCESCDL1 |
        DCMIPP_CSI_IT_EESCDL1 | DCMIPP_CSI_IT_ESOTSYNCDL1 |
        DCMIPP_CSI_IT_ESOTDL1 | DCMIPP_CSI_IT_ECTRLDL0 |
        DCMIPP_CSI_IT_ESYNCESCDL0 | DCMIPP_CSI_IT_EESCDL0 |
        DCMIPP_CSI_IT_ESOTSYNCDL0 | DCMIPP_CSI_IT_ESOTDL0;
    const uint32_t pending0 = status0 & CSI->IER0 & errors0;
    const uint32_t pending1 = status1 & CSI->IER1 & errors1;
    if ((pending1 & DCMIPP_CSI_IT_ESOTSYNCDL0) != 0U) ++g_camera_csi_sot_sync_dl0_count;
    if ((pending1 & DCMIPP_CSI_IT_ESOTSYNCDL1) != 0U) ++g_camera_csi_sot_sync_dl1_count;
    if ((pending1 & DCMIPP_CSI_IT_ESOTDL0) != 0U) ++g_camera_csi_sot_dl0_count;
    if ((pending1 & DCMIPP_CSI_IT_ESOTDL1) != 0U) ++g_camera_csi_sot_dl1_count;
    HAL_DCMIPP_CSI_IRQHandler(&hcamera_dcmipp);
    if (pending0 != 0U || pending1 != 0U) {
        g_camera_csi_last_status = status0;
        g_camera_csi_last_status1 = status1;
        g_camera_csi_last_pending_status = pending0;
        g_camera_csi_last_pending_status1 = pending1;
        g_camera_csi_last_error_code = hcamera_dcmipp.ErrorCode;
        ++g_camera_csi_error_count;
        g_last_csi_error_tick = HAL_GetTick();
        g_csi_fault_pending = true;
        // HAL_DCMIPP_CSI_IRQHandler() disables and clears the faulty CSI
        // interrupt source.  Do not re-arm it here: repeated D-PHY errors
        // must not turn into an interrupt storm.  The normal frame watchdog
        // and recovery path remain enabled as the fail-safe.
    }
}

extern "C" HAL_StatusTypeDef MX_DCMIPP_ClockConfig(DCMIPP_HandleTypeDef *hdcmipp)
{
    UNUSED(hdcmipp);
    RCC_PeriphCLKInitTypeDef clock = {};
    clock.PeriphClockSelection = RCC_PERIPHCLK_DCMIPP;
    clock.DcmippClockSelection = RCC_DCMIPPCLKSOURCE_IC17;
    clock.ICSelection[RCC_IC17].ClockSelection = RCC_ICCLKSOURCE_PLL1;
    clock.ICSelection[RCC_IC17].ClockDivider = 4U;
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK) return HAL_ERROR;
    clock.PeriphClockSelection = RCC_PERIPHCLK_CSI;
    clock.ICSelection[RCC_IC18].ClockSelection = RCC_ICCLKSOURCE_PLL1;
    clock.ICSelection[RCC_IC18].ClockDivider = 60U;
    return HAL_RCCEx_PeriphCLKConfig(&clock);
}
