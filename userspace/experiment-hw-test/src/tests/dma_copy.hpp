#pragma once

#include "board.hpp"

#include <cstdio>

namespace experiment::hwtest::tests {

inline Result CheckDmaCopy(DMA_Channel_TypeDef *channel, const Context &context)
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
    if (HAL_DMA_Init(&handle) == HAL_OK
        && HAL_DMA_ConfigChannelAttributes(
               &handle, DMA_CHANNEL_PRIV | DMA_CHANNEL_SEC | DMA_CHANNEL_SRC_SEC | DMA_CHANNEL_DEST_SEC
           ) == HAL_OK) {
        constexpr std::uint32_t kCtr1Mask = DMA_CTR1_DINC | DMA_CTR1_DDW_LOG2 | DMA_CTR1_SINC
                                            | DMA_CTR1_SDW_LOG2 | DMA_CTR1_DAP | DMA_CTR1_SAP
                                            | DMA_CTR1_DBL_1 | DMA_CTR1_SBL_1;
        constexpr std::uint32_t kCtr2Mask = DMA_CTR2_TCEM | DMA_CTR2_BREQ | DMA_CTR2_REQSEL | DMA_CTR2_DREQ
                                            | DMA_CTR2_SWREQ | DMA_CTR2_TRIGPOL | DMA_CTR2_TRIGSEL
                                            | DMA_CTR2_TRIGM | DMA_CTR2_PFREQ;
        const auto expected_ctr1 = handle.Init.DestInc | handle.Init.DestDataWidth | handle.Init.SrcInc
                                   | handle.Init.SrcDataWidth | handle.Init.TransferAllocatedPort;
        const auto expected_ctr2 = handle.Init.BlkHWRequest | (handle.Init.Request & DMA_CTR2_REQSEL)
                                   | handle.Init.TransferEventMode | handle.Init.Mode | DMA_CTR2_SWREQ;
        const auto ccr = channel->CCR;
        const auto ctr1 = channel->CTR1;
        const auto ctr2 = channel->CTR2;
        char register_trace[144];
        std::snprintf(
            register_trace,
            sizeof(register_trace),
            "TRACE dma regs ccr=%08lx ctr1=%08lx ctr2=%08lx",
            static_cast<unsigned long>(ccr),
            static_cast<unsigned long>(ctr1),
            static_cast<unsigned long>(ctr2)
        );
        context.Trace(register_trace);
        if ((ccr & DMA_CCR_PRIO) != handle.Init.Priority || (ccr & DMA_CCR_EN) != 0U
            || (ctr1 & kCtr1Mask) != (expected_ctr1 & kCtr1Mask)
            || (ctr2 & kCtr2Mask) != (expected_ctr2 & kCtr2Mask)) {
            static char detail[112];
            std::snprintf(
                detail,
                sizeof(detail),
                "dma-reg ccr=%08lx ctr1=%08lx/%08lx ctr2=%08lx/%08lx",
                static_cast<unsigned long>(ccr),
                static_cast<unsigned long>(ctr1 & kCtr1Mask),
                static_cast<unsigned long>(expected_ctr1 & kCtr1Mask),
                static_cast<unsigned long>(ctr2 & kCtr2Mask),
                static_cast<unsigned long>(expected_ctr2 & kCtr2Mask)
            );
            result = {Outcome::kFail, detail};
        } else {
            result = {Outcome::kPass, "register-config-and-seven-copy-lengths-guards-cache"};
            static constexpr std::size_t lengths[] = {1, 3, 31, 32, 33, 255, 256};
            for (const auto length : lengths) {
                const std::size_t offset = length == 256 ? 32 : 33;
                for (std::size_t index = 0; index < sizeof(source); ++index) {
                    source[index] = static_cast<std::uint8_t>((index * 37U) ^ 0xa5U);
                    destination[index] = 0x5a;
                }
                SCB_CleanDCache_by_Addr(source, sizeof(source));
                SCB_CleanInvalidateDCache_by_Addr(destination, sizeof(destination));
                if (HAL_DMA_Start(
                        &handle,
                        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(source + offset)),
                        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(destination + offset)),
                        static_cast<std::uint32_t>(length)
                    ) != HAL_OK
                    || HAL_DMA_PollForTransfer(&handle, HAL_DMA_FULL_TRANSFER, 100) != HAL_OK) {
                    HAL_DMA_Abort(&handle);
                    result = {Outcome::kFail, "dma-transfer-or-timeout"};
                    break;
                }
                if (handle.ErrorCode != HAL_DMA_ERROR_NONE) {
                    static char detail[72];
                    std::snprintf(
                        detail,
                        sizeof(detail),
                        "dma-hal-error err=%08lx csr=%08lx",
                        static_cast<unsigned long>(handle.ErrorCode),
                        static_cast<unsigned long>(channel->CSR)
                    );
                    result = {Outcome::kFail, detail};
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
                if (result.outcome == Outcome::kFail) {
                    break;
                }
            }
        }
    }
    if (HAL_DMA_DeInit(&handle) != HAL_OK) {
        result = {Outcome::kFail, "dma-deinitialization"};
    }
    return result;
}

}
