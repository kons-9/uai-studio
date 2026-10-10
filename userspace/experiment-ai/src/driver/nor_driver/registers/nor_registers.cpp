#include "driver/nor_driver/registers/nor_registers.hpp"
#include "common/log.hpp"

#include <cstdio>

extern "C" {
#include "driver/c_bsp/xspi_bsp.h"
#include "stm32n6xx_hal.h"
#include <tm/tmonitor.h>
}

namespace uai::ai::nor::registers {
namespace {

void DebugPrint(const char *message)
{
    UAI_LOG_TEXT(uai::ai::common::LogLevel::kDebug, reinterpret_cast<UB *>(const_cast<char *>(message)));
}

} // namespace

int NorRegisterLayer::Initialize()
{
    BSP_XSPI_NOR_Init_t nor_init = {};
    nor_init.InterfaceMode = BSP_XSPI_NOR_OPI_MODE;
    nor_init.TransferRate = BSP_XSPI_NOR_DTR_TRANSFER;

    const int32_t status = BSP_XSPI_NOR_Init(0U, &nor_init);
    char message[256];
    (void)std::snprintf(
        message,
        sizeof(message),
        "boot: nor bsp init=%ld stage=%ld reset=%ld/%ld err=%lx sr=%lx cr=%lx iom=%lx diag=%lu/%lu d_err=%lx d_sr=%lx "
        "d_ccr=%lx\r\n",
        static_cast<long>(status),
        static_cast<long>(uai_nor_bsp_stage),
        static_cast<long>(uai_nor_reset_stage),
        static_cast<long>(uai_nor_reset_result),
        static_cast<unsigned long>(uai_nor_reset_hal_error),
        static_cast<unsigned long>(uai_nor_reset_sr),
        static_cast<unsigned long>(uai_nor_reset_cr),
        static_cast<unsigned long>(XSPIM->CR),
        static_cast<unsigned long>(uai_nor_diag_code),
        static_cast<unsigned long>(uai_nor_diag_stage),
        static_cast<unsigned long>(uai_nor_diag_hal_error),
        static_cast<unsigned long>(uai_nor_diag_sr),
        static_cast<unsigned long>(uai_nor_diag_ccr)
    );
    DebugPrint(message);
    return status == BSP_ERROR_NONE ? 0 : -1;
}

int NorRegisterLayer::Read(
    std::uint8_t *buffer,
    std::uint32_t address,
    std::size_t size
)
{
    if (buffer == nullptr || size == 0U) {
        return -1;
    }
    return BSP_XSPI_NOR_Read(0U, buffer, address, size) == BSP_ERROR_NONE ? 0 : -1;
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
