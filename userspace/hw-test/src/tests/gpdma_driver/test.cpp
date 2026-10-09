#include "tests/dma_copy.hpp"

namespace experiment::hwtest::tests::gpdma_driver {

Result Run(const Context &context)
{
    SecurePeripheral(RIF_RCC_PERIPH_INDEX_GPDMA1);
    __HAL_RCC_GPDMA1_CLK_ENABLE();
    __HAL_RCC_GPDMA1_FORCE_RESET();
    __HAL_RCC_GPDMA1_RELEASE_RESET();
    const auto result = CheckDmaCopy(GPDMA1_Channel0, context);
    __HAL_RCC_GPDMA1_FORCE_RESET();
    __DSB();
    __HAL_RCC_GPDMA1_RELEASE_RESET();
    __HAL_RCC_GPDMA1_CLK_DISABLE();
    return result;
}

}
