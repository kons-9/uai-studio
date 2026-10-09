#include "tests/board.hpp"

#include <cstdio>

namespace experiment::hwtest::tests::crc_driver {

Result Run(const Context &context)
{
    alignas(4) static std::uint8_t input[12] = "123456789";
    SecurePeripheral(RIF_RISC_PERIPH_INDEX_CRC);
    __HAL_RCC_CRC_CLK_ENABLE();
    __HAL_RCC_CRC_FORCE_RESET();
    __HAL_RCC_CRC_RELEASE_RESET();
    CRC_HandleTypeDef handle{};
    handle.Instance = CRC;
    handle.Init.DefaultPolynomialUse = DEFAULT_POLYNOMIAL_ENABLE;
    handle.Init.DefaultInitValueUse = DEFAULT_INIT_VALUE_ENABLE;
    handle.Init.InputDataInversionMode = CRC_INPUTDATA_INVERSION_NONE;
    handle.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_DISABLE;
    handle.InputDataFormat = CRC_INPUTDATA_FORMAT_BYTES;
    Result result{Outcome::kFail, "crc-initialization"};
    if (HAL_CRC_Init(&handle) == HAL_OK) {
        const auto control = CRC->CR;
        const auto polynomial = CRC->POL;
        const auto initial = CRC->INIT;
        char trace[96];
        std::snprintf(
            trace,
            sizeof(trace),
            "TRACE crc regs cr=%08lx pol=%08lx init=%08lx",
            static_cast<unsigned long>(control),
            static_cast<unsigned long>(polynomial),
            static_cast<unsigned long>(initial)
        );
        context.Trace(trace);
        const auto inversion_mask = CRC_CR_RTYPE_IN | CRC_CR_REV_IN | CRC_CR_RTYPE_OUT | CRC_CR_REV_OUT;
        if ((control & inversion_mask) != 0U || polynomial != DEFAULT_CRC32_POLY || initial != DEFAULT_CRC_INITVALUE) {
            static char detail[96];
            std::snprintf(
                detail,
                sizeof(detail),
                "crc-config cr=%08lx pol=%08lx init=%08lx",
                static_cast<unsigned long>(control),
                static_cast<unsigned long>(polynomial),
                static_cast<unsigned long>(initial)
            );
            result = {Outcome::kFail, detail};
        } else {
            const auto first = HAL_CRC_Calculate(&handle, reinterpret_cast<std::uint32_t *>(input), 9);
            const auto second = HAL_CRC_Calculate(&handle, reinterpret_cast<std::uint32_t *>(input), 9);
            result = first == 0x0376e6e7U && second == first
                ? Result{Outcome::kPass, "crc32-register-config-known-vector-and-reset"}
                : Result{Outcome::kFail, "crc32-known-vector-mismatch"};
        }
    }
    if (HAL_CRC_DeInit(&handle) != HAL_OK) {
        result = {Outcome::kFail, "crc-deinitialization"};
    }
    __HAL_RCC_CRC_CLK_DISABLE();
    return result;
}

}
