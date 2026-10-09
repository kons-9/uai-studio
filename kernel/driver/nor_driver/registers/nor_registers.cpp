#include "driver/nor_driver/registers/nor_registers.hpp"
#include "middleware/foundation/log.hpp"

#include <cstdio>

extern "C" {
#include "driver/c_bsp/xspi_bsp.h"
#include "stm32n6xx_hal.h"
extern XSPI_NOR_Ctx_t XSPI_Nor_Ctx[];
}

namespace uai::ai::nor::registers {
namespace {

void DebugPrint(const char *message)
{
    UAI_LOG_TEXT(uai::ai::common::LogLevel::kDebug, message);
}

} // namespace

int NorRegisterLayer::Initialize()
{
    __HAL_RCC_RIFSC_CLK_ENABLE();
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_XSPI2, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    BSP_XSPI_NOR_Init_t nor_init = {};
    nor_init.InterfaceMode = BSP_XSPI_NOR_OPI_MODE;
    nor_init.TransferRate = BSP_XSPI_NOR_DTR_TRANSFER;

    const int32_t status = BSP_XSPI_NOR_Init(0U, &nor_init);
    char message[160];
    (void)std::snprintf(
        message,
        sizeof(message),
        "boot: nor bsp init=%ld stage=%ld reset=%ld/%ld err=%lx sr=%lx cr=%lx iom=%lx\r\n",
        static_cast<long>(status),
        static_cast<long>(uai_nor_bsp_stage),
        static_cast<long>(uai_nor_reset_stage),
        static_cast<long>(uai_nor_reset_result),
        static_cast<unsigned long>(uai_nor_reset_hal_error),
        static_cast<unsigned long>(uai_nor_reset_sr),
        static_cast<unsigned long>(uai_nor_reset_cr),
        static_cast<unsigned long>(XSPIM->CR)
    );
    DebugPrint(message);
    const auto control = XSPI2->CR;
    const bool mapped = XSPI_Nor_Ctx[0].IsInitialized == XSPI_ACCESS_MMP;
    return status == BSP_ERROR_NONE && XSPI_Nor_Ctx[0].IsInitialized != XSPI_ACCESS_NONE
            && XSPI_Nor_Ctx[0].InterfaceMode == BSP_XSPI_NOR_OPI_MODE
            && XSPI_Nor_Ctx[0].TransferRate == BSP_XSPI_NOR_DTR_TRANSFER && (control & XSPI_CR_EN) != 0U
            && (mapped || (XSPI2->SR & XSPI_SR_BUSY) == 0U)
        ? 0
        : -1;
}

int NorRegisterLayer::Read(
    std::uint8_t *buffer,
    std::uint32_t address,
    std::size_t size
)
{
    constexpr std::uint32_t capacity = 128U * 1024U * 1024U;
    if (buffer == nullptr || size == 0U || address >= capacity || size > capacity - address) {
        return -1;
    }
    return BSP_XSPI_NOR_Read(0U, buffer, address, size) == BSP_ERROR_NONE ? 0 : -1;
}

Diagnostic NorRegisterLayer::Diagnostics() const
{
    return {
        uai_nor_diag_code,
        uai_nor_diag_stage,
        uai_nor_diag_component_result,
        uai_nor_diag_hal_error,
        uai_nor_diag_hal_state,
        uai_nor_diag_sr,
        uai_nor_diag_cr,
        uai_nor_diag_ccr
    };
}

int NorRegisterLayer::EnableMemoryMappedMode()
{
    const int32_t status = BSP_XSPI_NOR_EnableMemoryMappedMode(0U);
    char message[160];
    (void)std::snprintf(
        message,
        sizeof(message),
        "boot: nor bsp map=%ld stage=%ld sr=%lx cr=%lx\r\n",
        static_cast<long>(status),
        static_cast<long>(uai_nor_bsp_stage),
        static_cast<unsigned long>(XSPI2->SR),
        static_cast<unsigned long>(XSPI2->CR)
    );
    DebugPrint(message);
    return status == BSP_ERROR_NONE ? 0 : -1;
}

} // namespace uai::ai::nor::registers
