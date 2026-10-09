#include "tests/board.hpp"

namespace experiment::hwtest::tests::crc_driver {

Result Run(const Context &)
{
    alignas(4) static constexpr std::uint8_t input[12] = "123456789";
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
        const auto first = HAL_CRC_Calculate(&handle, reinterpret_cast<const std::uint32_t *>(input), 9);
        const auto second = HAL_CRC_Calculate(&handle, reinterpret_cast<const std::uint32_t *>(input), 9);
        result = first == 0x0376e6e7U && second == first
            ? Result{Outcome::kPass, "crc32-mpeg2-known-vector-and-reset"}
            : Result{Outcome::kFail, "crc32-known-vector-mismatch"};
    }
    if (HAL_CRC_DeInit(&handle) != HAL_OK) { result = {Outcome::kFail, "crc-deinitialization"}; }
    __HAL_RCC_CRC_CLK_DISABLE();
    return result;
}

}