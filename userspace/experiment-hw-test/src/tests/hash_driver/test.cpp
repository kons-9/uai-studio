#include "tests/board.hpp"

#include <cstdio>

namespace experiment::hwtest::tests::hash_driver {

Result Run(const Context &context)
{
    alignas(4) static constexpr std::uint8_t short_input[4] = {'a', 'b', 'c', 0};
    alignas(4) static constexpr std::uint8_t block_input[60] =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    static constexpr std::uint8_t short_digest[32] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                                                      0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                                                      0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    static constexpr std::uint8_t block_digest[32] = {0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8, 0xe5, 0xc0, 0x26,
                                                      0x93, 0x0c, 0x3e, 0x60, 0x39, 0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff,
                                                      0x21, 0x67, 0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1};
    SecurePeripheral(RIF_RISC_PERIPH_INDEX_HASH);
    __HAL_RCC_HASH_CLK_ENABLE();
    __HAL_RCC_HASH_FORCE_RESET();
    __HAL_RCC_HASH_RELEASE_RESET();
    HASH_HandleTypeDef handle{};
    handle.Instance = HASH;
    handle.Init.DataType = HASH_BYTE_SWAP;
    handle.Init.Algorithm = HASH_ALGOSELECTION_SHA256;
    alignas(4) std::uint8_t digest[32]{};
    Result result{Outcome::kFail, "hash-initialization"};
    if (HAL_HASH_Init(&handle) == HAL_OK) {
        const auto control = HASH->CR;
        char trace[64];
        std::snprintf(trace, sizeof(trace), "TRACE hash regs cr=%08lx", static_cast<unsigned long>(control));
        context.Trace(trace);
        constexpr std::uint32_t kHashConfigMask = HASH_CR_ALGO | HASH_CR_DATATYPE;
        constexpr std::uint32_t kHashExpected = HASH_ALGOSELECTION_SHA256 | HASH_BYTE_SWAP;
        if ((control & kHashConfigMask) != kHashExpected) {
            static char detail[64];
            std::snprintf(detail, sizeof(detail), "hash-config cr=%08lx", static_cast<unsigned long>(control));
            result = {Outcome::kFail, detail};
        } else if (HAL_HASH_Start(&handle, short_input, 3, digest, 100) != HAL_OK
            || std::memcmp(digest, short_digest, sizeof(digest)) != 0) {
            result = {Outcome::kFail, "sha256-short-vector"};
        } else {
            context.Progress(1, 2);
            if (HAL_HASH_Start(&handle, block_input, 56, digest, 100) != HAL_OK
                || std::memcmp(digest, block_digest, sizeof(digest)) != 0) {
                result = {Outcome::kFail, "sha256-block-padding-vector"};
            } else {
                context.Progress(2, 2);
                const auto final_control = HASH->CR;
                const auto status = HASH->SR;
                if ((final_control & kHashConfigMask) != kHashExpected || (status & HASH_SR_BUSY) != 0U) {
                    static char detail[72];
                    std::snprintf(
                        detail,
                        sizeof(detail),
                        "hash-status cr=%08lx sr=%08lx",
                        static_cast<unsigned long>(final_control),
                        static_cast<unsigned long>(status)
                    );
                    result = {Outcome::kFail, detail};
                } else {
                    result = {Outcome::kPass, "sha256-config-and-two-known-answer-vectors"};
                }
            }
        }
    }
    if (HAL_HASH_DeInit(&handle) != HAL_OK) {
        result = {Outcome::kFail, "hash-deinitialization"};
    }
    __HAL_RCC_HASH_CLK_DISABLE();
    return result;
}

}
