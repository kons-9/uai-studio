#include "driver/dma2d_driver/dma2d_driver.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
}

namespace uai::ai::dma2d {
namespace {

void Reset()
{
    __HAL_RCC_DMA2D_FORCE_RESET();
    __DSB();
    __HAL_RCC_DMA2D_RELEASE_RESET();
    __DSB();
}

bool ConfigureLayer(
    DMA2D_HandleTypeDef &handle,
    std::uint32_t index,
    const image_processing::Image &image,
    std::uint8_t alpha
)
{
    auto &layer = handle.LayerCfg[index];
    layer.InputOffset = image.stride / image_processing::PixelBytes(image.format) - image.width;
    layer.InputColorMode = image.format == image_processing::Format::kRgb565 ? DMA2D_INPUT_RGB565 : DMA2D_INPUT_RGB888;
    layer.AlphaMode = DMA2D_REPLACE_ALPHA;
    layer.InputAlpha = alpha;
    layer.RedBlueSwap = image.format == image_processing::Format::kRgb888 ? DMA2D_RB_SWAP : DMA2D_RB_REGULAR;
    return HAL_DMA2D_ConfigLayer(&handle, index) == HAL_OK;
}

}

common::Error Dma2dDriver::Initialize(const Writer &writer)
{
    const common::Error status = Dma2dManagement::Instance().Validate(writer);
    if (!status.Ok()) {
        return status;
    }
    if (initialized_) {
        return {common::ErrorCode::kAlreadyInitialized};
    }
    __HAL_RCC_RIFSC_CLK_ENABLE();
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_DMA2D, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    RIMC_MasterConfig_t master{};
    master.MasterCID = RIF_CID_1;
    master.SecPriv = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DMA2D, &master);
    __HAL_RCC_DMA2D_CLK_ENABLE();
    __HAL_RCC_DMA2D_CLK_SLEEP_ENABLE();
    initialized_ = true;
    return {};
}

void Dma2dDriver::KeepClocksOnSleep(const Writer &writer) const
{
    if (initialized_ && Dma2dManagement::Instance().Validate(writer).Ok()) {
        __HAL_RCC_DMA2D_CLK_SLEEP_ENABLE();
    }
}

common::Error Dma2dDriver::Transfer(
    const Request &request,
    std::uint32_t timeout_ms,
    const Writer &writer
)
{
    const common::Error ownership = Dma2dManagement::Instance().Validate(writer);
    if (!ownership.Ok()) {
        return ownership;
    }
    if (!initialized_) {
        return {common::ErrorCode::kNotInitialized};
    }
    if (!ValidateRequest(request, timeout_ms)) {
        return {common::ErrorCode::kInvalidArgument};
    }
    using namespace image_processing;
    const auto &destination = request.destination;
    DMA2D_HandleTypeDef handle{};
    handle.Instance = DMA2D;
    handle.Init.Mode = request.operation == Operation::kFill ? DMA2D_R2M
        : request.operation == Operation::kBlend             ? DMA2D_M2M_BLEND
                                                             : DMA2D_M2M_PFC;
    handle.Init.ColorMode = destination.format == Format::kRgb565 ? DMA2D_OUTPUT_RGB565 : DMA2D_OUTPUT_RGB888;
    handle.Init.OutputOffset = destination.stride / PixelBytes(destination.format) - destination.width;
    handle.Init.RedBlueSwap = request.operation != Operation::kFill && destination.format == Format::kRgb888
        ? DMA2D_RB_SWAP
        : DMA2D_RB_REGULAR;
    if (HAL_DMA2D_Init(&handle) != HAL_OK
        || (request.operation != Operation::kFill && !ConfigureLayer(handle, 1, request.source, request.alpha))
        || (request.operation == Operation::kBlend && !ConfigureLayer(handle, 0, request.background, 255))) {
        Reset();
        return {common::ErrorCode::kHardware};
    }
    if (request.operation != Operation::kFill) {
        SCB_CleanDCache_by_Addr(request.source.data, static_cast<std::int32_t>(request.source.bytes));
    }
    if (request.operation == Operation::kBlend) {
        SCB_CleanDCache_by_Addr(request.background.data, static_cast<std::int32_t>(request.background.bytes));
    }
    SCB_CleanInvalidateDCache_by_Addr(destination.data, static_cast<std::int32_t>(destination.bytes));
    __DSB();
    auto source = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(request.source.data));
    if (request.operation == Operation::kFill) {
        source = request.color;
        if (destination.format == Format::kRgb888) {
            source =
                ((request.color & 0x0000ff) << 16) | (request.color & 0x00ff00) | ((request.color & 0xff0000) >> 16);
        }
    }
    const auto output = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(destination.data));
    HAL_StatusTypeDef status = request.operation == Operation::kBlend
        ? HAL_DMA2D_BlendingStart(
              &handle,
              source,
              static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(request.background.data)),
              output,
              destination.width,
              destination.height
          )
        : HAL_DMA2D_Start(&handle, source, output, destination.width, destination.height);
    if (status == HAL_OK) {
        status = HAL_DMA2D_PollForTransfer(&handle, timeout_ms);
    }
    if (status != HAL_OK && HAL_DMA2D_Abort(&handle) != HAL_OK) {
        Reset();
    }
    __DSB();
    SCB_InvalidateDCache_by_Addr(destination.data, static_cast<std::int32_t>(destination.bytes));
    __DSB();
    return {
        status == HAL_OK            ? common::ErrorCode::kOk
            : status == HAL_TIMEOUT ? common::ErrorCode::kTimeout
                                    : common::ErrorCode::kHardware
    };
}

}