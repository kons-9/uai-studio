#include "hwtest.hpp"

extern "C" {
#include "stm32n6570_discovery_xspi.h"
}

namespace experiment::hwtest::tests::nor_driver {

Result Run(const Context &)
{
    BSP_XSPI_NOR_Init_t configuration{};
    configuration.InterfaceMode = BSP_XSPI_NOR_OPI_MODE;
    configuration.TransferRate = BSP_XSPI_NOR_DTR_TRANSFER;
    if (BSP_XSPI_NOR_Init(0, &configuration) != BSP_ERROR_NONE) {
        return {Outcome::kFail, "nor-initialization"};
    }
    std::uint8_t first[256]{}, second[256]{};
    if (BSP_XSPI_NOR_Read(0, first, 0, sizeof(first)) != BSP_ERROR_NONE
        || BSP_XSPI_NOR_Read(0, second, 0, sizeof(second)) != BSP_ERROR_NONE) {
        return {Outcome::kFail, "nor-read"};
    }
    return std::memcmp(first, second, sizeof(first)) == 0 ? Result{Outcome::kPass, "read-only-repeat-stable"}
                                                          : Result{Outcome::kFail, "nor-unstable"};
}

}