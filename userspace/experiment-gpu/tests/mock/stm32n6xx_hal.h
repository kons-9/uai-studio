#pragma once
#include <cstdint>
#include <string>
enum HAL_StatusTypeDef {
    HAL_OK,
    HAL_ERROR,
    HAL_TIMEOUT
};
enum {
    DMA2D_R2M,
    DMA2D_M2M_BLEND,
    DMA2D_M2M_PFC,
    DMA2D_OUTPUT_RGB565,
    DMA2D_OUTPUT_RGB888,
    DMA2D_INPUT_RGB565,
    DMA2D_INPUT_RGB888,
    DMA2D_RB_SWAP,
    DMA2D_RB_REGULAR,
    DMA2D_REPLACE_ALPHA
};
inline void *DMA2D = nullptr;
struct DMA2D_InitTypeDef {
    std::uint32_t Mode, ColorMode, OutputOffset, RedBlueSwap;
};
struct DMA2D_LayerCfgTypeDef {
    std::uint32_t InputOffset, InputColorMode, AlphaMode, InputAlpha, RedBlueSwap;
};
struct DMA2D_HandleTypeDef {
    void *Instance;
    DMA2D_InitTypeDef Init;
    DMA2D_LayerCfgTypeDef LayerCfg[2];
};
inline std::string calls;
inline HAL_StatusTypeDef poll_status = HAL_OK, abort_status = HAL_OK;
inline std::uint32_t fill_color = 0;
inline DMA2D_HandleTypeDef configured{};
struct Dwt {
    std::uint32_t CYCCNT = 0;
};
inline Dwt dwt;
inline Dwt *DWT = &dwt;
inline void __HAL_RCC_DMA2D_CLK_ENABLE()
{
    calls += "clock ";
}
inline void __HAL_RCC_DMA2D_FORCE_RESET()
{
    calls += "reset ";
}
inline void __HAL_RCC_DMA2D_RELEASE_RESET()
{
    calls += "release ";
}
inline void __DSB()
{
    calls += "barrier ";
}
inline void SCB_CleanDCache_by_Addr(
    void *,
    std::int32_t
)
{
    calls += "clean ";
}
inline void SCB_CleanInvalidateDCache_by_Addr(
    void *,
    std::int32_t
)
{
    calls += "prepare ";
}
inline void SCB_InvalidateDCache_by_Addr(
    void *,
    std::int32_t
)
{
    calls += "invalidate ";
}
inline HAL_StatusTypeDef HAL_DMA2D_Init(DMA2D_HandleTypeDef *)
{
    calls += "init ";
    return HAL_OK;
}
inline HAL_StatusTypeDef HAL_DMA2D_ConfigLayer(
    DMA2D_HandleTypeDef *,
    std::uint32_t
)
{
    calls += "layer ";
    return HAL_OK;
}
inline HAL_StatusTypeDef HAL_DMA2D_Start(
    DMA2D_HandleTypeDef *handle,
    std::uint32_t source,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t
)
{
    configured = *handle;
    fill_color = source;
    calls += "start ";
    return HAL_OK;
}
inline HAL_StatusTypeDef HAL_DMA2D_BlendingStart(
    DMA2D_HandleTypeDef *handle,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t
)
{
    configured = *handle;
    calls += "blend ";
    return HAL_OK;
}
inline HAL_StatusTypeDef HAL_DMA2D_PollForTransfer(
    DMA2D_HandleTypeDef *,
    std::uint32_t
)
{
    calls += "poll ";
    return poll_status;
}
inline HAL_StatusTypeDef HAL_DMA2D_Abort(DMA2D_HandleTypeDef *)
{
    calls += "abort ";
    return abort_status;
}