#include "tests/dma_copy.hpp"

namespace experiment::hwtest::tests::hpdma_driver {

Result Run(const Context &context)
{
    SecurePeripheral(RIF_RCC_PERIPH_INDEX_HPDMA1);
    __HAL_RCC_HPDMA1_CLK_ENABLE();
    __HAL_RCC_HPDMA1_FORCE_RESET();
    __HAL_RCC_HPDMA1_RELEASE_RESET();
    const auto result = CheckDmaCopy(HPDMA1_Channel0, context);
    __HAL_RCC_HPDMA1_FORCE_RESET();
    __DSB();
    __HAL_RCC_HPDMA1_RELEASE_RESET();
    __HAL_RCC_HPDMA1_CLK_DISABLE();
    return result;
}

}
