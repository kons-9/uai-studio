#include "tests/dma_copy.hpp"

namespace experiment::hwtest::tests::hpdma_driver {

Result Run(const Context &context)
{
    return CheckDmaCopy(uai::ai::peripheral::DmaController::kHighPerformance, context);
}

}