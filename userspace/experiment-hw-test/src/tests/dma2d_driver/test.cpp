#include "tests/board.hpp"

namespace experiment::hwtest::tests::dma2d_driver {

Result Run(const Context &)
{
    alignas(32) static std::uint32_t source[80];
    alignas(32) static std::uint32_t destination[80];
    for (std::size_t index = 0; index < 80; ++index) {
        source[index] = 0xff000000U | (0x010307U * static_cast<std::uint32_t>(index));
        destination[index] = 0x55aa55aa;
    }
    SCB_CleanDCache_by_Addr(source, sizeof(source));
    SCB_CleanInvalidateDCache_by_Addr(destination, sizeof(destination));
    SecurePeripheral(RIF_RISC_PERIPH_INDEX_DMA2D);
    RIMC_MasterConfig_t master{};
    master.MasterCID = RIF_CID_1;
    master.SecPriv = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DMA2D, &master);
    __HAL_RCC_DMA2D_CLK_ENABLE();
    __HAL_RCC_DMA2D_FORCE_RESET();
    __DSB();
    __HAL_RCC_DMA2D_RELEASE_RESET();
    DMA2D_HandleTypeDef handle{};
    handle.Instance = DMA2D;
    handle.Init.Mode = DMA2D_M2M;
    handle.Init.ColorMode = DMA2D_OUTPUT_ARGB8888;
    handle.LayerCfg[1].InputColorMode = DMA2D_INPUT_ARGB8888;
    handle.LayerCfg[1].AlphaMode = DMA2D_NO_MODIF_ALPHA;
    handle.LayerCfg[1].InputAlpha = 0xff;
    Result result{Outcome::kFail, "dma2d-initialization"};
    if (HAL_DMA2D_Init(&handle) == HAL_OK && HAL_DMA2D_ConfigLayer(&handle, 1) == HAL_OK) {
        if (HAL_DMA2D_Start(&handle,
                static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(source + 8)),
                static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(destination + 8)), 8, 8) != HAL_OK ||
            HAL_DMA2D_PollForTransfer(&handle, 100) != HAL_OK) {
            HAL_DMA2D_Abort(&handle);
            result = {Outcome::kFail, "dma2d-transfer-or-timeout"};
        } else {
            SCB_InvalidateDCache_by_Addr(source, sizeof(source));
            SCB_InvalidateDCache_by_Addr(destination, sizeof(destination));
            result = {Outcome::kPass, "dma-copy-cache-and-guards"};
            for (std::size_t index = 0; index < 80; ++index) {
                const auto original = 0xff000000U | (0x010307U * static_cast<std::uint32_t>(index));
                const auto expected = index >= 8 && index < 72 ? original : 0x55aa55aa;
                if (source[index] != original || destination[index] != expected) {
                    result = {Outcome::kFail, "dma2d-data-or-guard-mismatch"};
                    break;
                }
            }
        }
    }
    __HAL_RCC_DMA2D_FORCE_RESET();
    __DSB();
    __HAL_RCC_DMA2D_RELEASE_RESET();
    if (HAL_DMA2D_DeInit(&handle) != HAL_OK) { result = {Outcome::kFail, "dma2d-deinitialization"}; }
    __HAL_RCC_DMA2D_CLK_DISABLE();
    return result;
}

}