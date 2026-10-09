#pragma once

#include "board.hpp"

namespace experiment::hwtest::tests {

inline Result CheckDmaCopy(DMA_Channel_TypeDef *channel)
{
    alignas(32) static std::uint8_t source[320];
    alignas(32) static std::uint8_t destination[320];
    DMA_HandleTypeDef handle{};
    handle.Instance = channel;
    handle.Init.Request = DMA_REQUEST_SW;
    handle.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    handle.Init.Direction = DMA_MEMORY_TO_MEMORY;
    handle.Init.SrcInc = DMA_SINC_INCREMENTED;
    handle.Init.DestInc = DMA_DINC_INCREMENTED;
    handle.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
    handle.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
    handle.Init.Priority = DMA_HIGH_PRIORITY;
    handle.Init.SrcBurstLength = 1;
    handle.Init.DestBurstLength = 1;
    handle.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT1 | DMA_DEST_ALLOCATED_PORT1;
    handle.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    handle.Init.Mode = DMA_NORMAL;
    Result result{Outcome::kFail, "dma-initialization"};
    if (HAL_DMA_Init(&handle) == HAL_OK &&
        HAL_DMA_ConfigChannelAttributes(&handle, DMA_CHANNEL_PRIV | DMA_CHANNEL_SEC |
                                        DMA_CHANNEL_SRC_SEC | DMA_CHANNEL_DEST_SEC) == HAL_OK) {
        result = {Outcome::kPass, "seven-byte-lengths-data-guards-and-cache"};
        static constexpr std::size_t lengths[] = {1, 3, 31, 32, 33, 255, 256};
        for (const auto length : lengths) {
            const std::size_t offset = length == 256 ? 32 : 33;
            for (std::size_t index = 0; index < sizeof(source); ++index) {
                source[index] = static_cast<std::uint8_t>((index * 37U) ^ 0xa5U);
                destination[index] = 0x5a;
            }
            SCB_CleanDCache_by_Addr(source, sizeof(source));
            SCB_CleanInvalidateDCache_by_Addr(destination, sizeof(destination));
            if (HAL_DMA_Start(&handle,
                    static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(source + offset)),
                    static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(destination + offset)),
                    static_cast<std::uint32_t>(length)) != HAL_OK ||
                HAL_DMA_PollForTransfer(&handle, HAL_DMA_FULL_TRANSFER, 100) != HAL_OK) {
                HAL_DMA_Abort(&handle);
                result = {Outcome::kFail, "dma-transfer-or-timeout"};
                break;
            }
            SCB_InvalidateDCache_by_Addr(source, sizeof(source));
            SCB_InvalidateDCache_by_Addr(destination, sizeof(destination));
            for (std::size_t index = 0; index < sizeof(source); ++index) {
                const auto original = static_cast<std::uint8_t>((index * 37U) ^ 0xa5U);
                const auto expected = index >= offset && index < offset + length ? original : 0x5a;
                if (source[index] != original || destination[index] != expected) {
                    result = {Outcome::kFail, "dma-data-or-guard-mismatch"};
                    break;
                }
            }
            if (result.outcome == Outcome::kFail) { break; }
        }
    }
    if (HAL_DMA_DeInit(&handle) != HAL_OK) { result = {Outcome::kFail, "dma-deinitialization"}; }
    return result;
}

}