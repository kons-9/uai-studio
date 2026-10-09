#pragma once
#include "operations.hpp"
#include "stm32n6xx_hal.h"

namespace experiment::graphics {

class Dma2d {
public:
    bool
    Run(const Request &request,
        std::uint32_t timeout_ms)
    {
        if (!Validate(request) || request.operation == Operation::kResize || !timeout_ms || timeout_ms > 1000
            || request.destination.width > 0x3fff || request.destination.height > 0xffff
            || !Cacheable(request.destination) || (request.operation != Operation::kFill && !Cacheable(request.source))
            || (request.operation == Operation::kBlend && !Cacheable(request.background))) {
            return false;
        }
        const auto &destination = request.destination;
        const auto offset = destination.stride / PixelBytes(destination.format) - destination.width;
        if (offset > 0x3fff) {
            return false;
        }
        __HAL_RCC_DMA2D_CLK_ENABLE();
        handle_ = {};
        handle_.Instance = DMA2D;
        handle_.Init.Mode = request.operation == Operation::kFill ? DMA2D_R2M
            : request.operation == Operation::kBlend              ? DMA2D_M2M_BLEND
                                                                  : DMA2D_M2M_PFC;
        handle_.Init.ColorMode = destination.format == Format::kRgb565 ? DMA2D_OUTPUT_RGB565 : DMA2D_OUTPUT_RGB888;
        handle_.Init.OutputOffset = offset;
        handle_.Init.RedBlueSwap = destination.format == Format::kRgb888 ? DMA2D_RB_SWAP : DMA2D_RB_REGULAR;
        if (HAL_DMA2D_Init(&handle_) != HAL_OK) {
            return false;
        }
        if (request.operation != Operation::kFill && !Layer(1, request.source, request.alpha)) {
            return false;
        }
        if (request.operation == Operation::kBlend && !Layer(0, request.background, 255)) {
            return false;
        }
        if (request.operation != Operation::kFill) {
            SCB_CleanDCache_by_Addr(request.source.data, request.source.bytes);
        }
        if (request.operation == Operation::kBlend) {
            SCB_CleanDCache_by_Addr(request.background.data, request.background.bytes);
        }
        SCB_CleanInvalidateDCache_by_Addr(destination.data, destination.bytes);
        __DSB();
        auto source = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(request.source.data));
        if (request.operation == Operation::kFill) {
            source = request.color;
        }
        const auto output = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(destination.data));
        const auto status = request.operation == Operation::kBlend
            ? HAL_DMA2D_BlendingStart(
                  &handle_,
                  source,
                  static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(request.background.data)),
                  output,
                  destination.width,
                  destination.height
              )
            : HAL_DMA2D_Start(&handle_, source, output, destination.width, destination.height);
        const bool complete = status == HAL_OK && HAL_DMA2D_PollForTransfer(&handle_, timeout_ms) == HAL_OK;
        if (!complete && HAL_DMA2D_Abort(&handle_) != HAL_OK) {
            __HAL_RCC_DMA2D_FORCE_RESET();
            __DSB();
            __HAL_RCC_DMA2D_RELEASE_RESET();
        }
        __DSB();
        SCB_InvalidateDCache_by_Addr(destination.data, destination.bytes);
        return complete;
    }

private:
    static bool Cacheable(const Image &image)
    {
        const auto address = reinterpret_cast<std::uintptr_t>(image.data);
        return address % 32 == 0 && image.bytes % 32 == 0 && image.bytes <= INT32_MAX
            && address <= UINT32_MAX - image.bytes;
    }
    bool Layer(
        std::uint32_t index,
        const Image &image,
        std::uint8_t alpha
    )
    {
        auto &layer = handle_.LayerCfg[index];
        layer.InputOffset = image.stride / PixelBytes(image.format) - image.width;
        if (layer.InputOffset > 0x3fff) {
            return false;
        }
        layer.InputColorMode = image.format == Format::kRgb565 ? DMA2D_INPUT_RGB565 : DMA2D_INPUT_RGB888;
        layer.AlphaMode = DMA2D_REPLACE_ALPHA;
        layer.InputAlpha = alpha;
        layer.RedBlueSwap = image.format == Format::kRgb888 ? DMA2D_RB_SWAP : DMA2D_RB_REGULAR;
        return HAL_DMA2D_ConfigLayer(&handle_, index) == HAL_OK;
    }
    DMA2D_HandleTypeDef handle_{};
};

}