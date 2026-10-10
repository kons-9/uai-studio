#include "tests/dma_copy.hpp"

namespace uai::hwtest::tests::gpdma_driver {

Result Run(const Context &context)
{
    return CheckDmaCopy(uai::ai::peripheral::DmaController::kGeneralPurpose, context);
}

}