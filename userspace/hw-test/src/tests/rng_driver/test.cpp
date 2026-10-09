#include "tests/board.hpp"

#include <cstdio>

namespace experiment::hwtest::tests::rng_driver {

Result Run(const Context &context)
{
    SecurePeripheral(RIF_RISC_PERIPH_INDEX_RNG);
    __HAL_RCC_RNG_CLK_ENABLE();
    __HAL_RCC_RNG_FORCE_RESET();
    __HAL_RCC_RNG_RELEASE_RESET();
    RNG_HandleTypeDef handle{};
    handle.Instance = RNG;
    handle.Init.ClockErrorDetection = RNG_CED_ENABLE;
    Result result{Outcome::kFail, "rng-initialization"};
    if (HAL_RNG_Init(&handle) == HAL_OK) {
        const auto control = RNG->CR;
        char trace[64];
        std::snprintf(trace, sizeof(trace), "TRACE rng regs cr=%08lx", static_cast<unsigned long>(control));
        context.Trace(trace);
        if ((control & RNG_CR_RNGEN) == 0U) {
            static char detail[64];
            std::snprintf(detail, sizeof(detail), "rng-rn-gen-clear cr=%08lx", static_cast<unsigned long>(control));
            result = {Outcome::kFail, detail};
        } else {
            const auto begin = context.clock();
            std::uint32_t first = 0;
            bool varied = false;
            result = {Outcome::kPass, "64-words-no-hardware-error"};
            for (unsigned index = 0; index < 64; ++index) {
                std::uint32_t sample = 0;
                if (context.Expired(begin, 500) || HAL_RNG_GenerateRandomNumber(&handle, &sample) != HAL_OK
                    || HAL_RNG_GetError(&handle) != HAL_RNG_ERROR_NONE) {
                    static char detail[72];
                    std::snprintf(
                        detail,
                        sizeof(detail),
                        "rng-read-error cr=%08lx sr=%08lx hal=%08lx",
                        static_cast<unsigned long>(RNG->CR),
                        static_cast<unsigned long>(RNG->SR),
                        static_cast<unsigned long>(HAL_RNG_GetError(&handle))
                    );
                    result = {Outcome::kFail, detail};
                    break;
                }
                if (index == 0) {
                    first = sample;
                }
                varied = varied || sample != first;
            }
            if (result.outcome == Outcome::kPass && !varied) {
                result = {Outcome::kFail, "rng-stuck-output"};
            }
            if (result.outcome == Outcome::kPass
                && ((RNG->CR & RNG_CR_RNGEN) == 0U || (RNG->SR & (RNG_SR_CEIS | RNG_SR_SEIS)) != 0U)) {
                static char detail[72];
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "rng-status cr=%08lx sr=%08lx",
                    static_cast<unsigned long>(RNG->CR),
                    static_cast<unsigned long>(RNG->SR)
                );
                result = {Outcome::kFail, detail};
            }
        }
    }
    if (HAL_RNG_DeInit(&handle) != HAL_OK) {
        result = {Outcome::kFail, "rng-deinitialization"};
    }
    __HAL_RCC_RNG_CLK_DISABLE();
    return result;
}

}
