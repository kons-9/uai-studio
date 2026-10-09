#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;

typedef struct {
    uint32_t Mode;
    uint32_t ColorMode;
    uint32_t OutputOffset;
    uint32_t RedBlueSwap;
} DMA2D_InitTypeDef;

typedef struct {
    uint32_t InputOffset;
    uint32_t InputColorMode;
    uint32_t AlphaMode;
    uint32_t InputAlpha;
    uint32_t RedBlueSwap;
} DMA2D_LayerCfgTypeDef;

typedef struct {
    void *Instance;
    DMA2D_InitTypeDef Init;
    DMA2D_LayerCfgTypeDef LayerCfg[2];
} DMA2D_HandleTypeDef;

typedef struct {
    uint32_t MasterCID;
    uint32_t SecPriv;
} RIMC_MasterConfig_t;

enum {
    DMA2D_R2M = 1,
    DMA2D_M2M_BLEND,
    DMA2D_M2M_PFC,
    DMA2D_OUTPUT_RGB565,
    DMA2D_OUTPUT_RGB888,
    DMA2D_INPUT_RGB565,
    DMA2D_INPUT_RGB888,
    DMA2D_REPLACE_ALPHA,
    DMA2D_RB_SWAP,
    DMA2D_RB_REGULAR,
    RIF_RISC_PERIPH_INDEX_DMA2D,
    RIF_MASTER_INDEX_DMA2D,
    RIF_CID_1,
    RIF_ATTRIBUTE_SEC = 0x100,
    RIF_ATTRIBUTE_PRIV = 0x200
};

#define DMA2D ((void *)0x50000000U)
#define __HAL_RCC_RIFSC_CLK_ENABLE() TestDma2dEvent("rif-clock")
#define __HAL_RCC_DMA2D_CLK_ENABLE() TestDma2dEvent("clock")
#define __HAL_RCC_DMA2D_CLK_SLEEP_ENABLE() TestDma2dEvent("sleep-clock")
#define __HAL_RCC_DMA2D_FORCE_RESET() TestDma2dEvent("reset")
#define __HAL_RCC_DMA2D_RELEASE_RESET() TestDma2dEvent("release-reset")
#define __DSB() TestDma2dEvent("barrier")

void TestDma2dEvent(const char *event);
void HAL_RIF_RISC_SetSlaveSecureAttributes(uint32_t peripheral, uint32_t attributes);
void HAL_RIF_RIMC_ConfigMasterAttributes(uint32_t peripheral, const RIMC_MasterConfig_t *config);
HAL_StatusTypeDef HAL_DMA2D_Init(DMA2D_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_DMA2D_ConfigLayer(DMA2D_HandleTypeDef *handle, uint32_t index);
HAL_StatusTypeDef HAL_DMA2D_Start(DMA2D_HandleTypeDef *handle, uint32_t source, uint32_t destination,
    uint32_t width, uint32_t height);
HAL_StatusTypeDef HAL_DMA2D_BlendingStart(DMA2D_HandleTypeDef *handle, uint32_t source, uint32_t background,
    uint32_t destination, uint32_t width, uint32_t height);
HAL_StatusTypeDef HAL_DMA2D_PollForTransfer(DMA2D_HandleTypeDef *handle, uint32_t timeout);
HAL_StatusTypeDef HAL_DMA2D_Abort(DMA2D_HandleTypeDef *handle);
void SCB_CleanDCache_by_Addr(void *address, int32_t bytes);
void SCB_CleanInvalidateDCache_by_Addr(void *address, int32_t bytes);
void SCB_InvalidateDCache_by_Addr(void *address, int32_t bytes);

#ifdef __cplusplus
}
#endif