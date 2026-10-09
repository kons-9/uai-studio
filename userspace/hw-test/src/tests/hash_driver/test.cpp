#include "hwtest.hpp"
#include "driver/peripheral_driver/peripheral_driver.hpp"

namespace experiment::hwtest::tests::hash_driver {

Result Run(const Context &context)
{
    alignas(4) static std::uint8_t short_input[4] = {'a', 'b', 'c', 0};
    alignas(4) static std::uint8_t block_input[60] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    static constexpr std::uint8_t short_digest[32] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                                                      0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                                                      0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    static constexpr std::uint8_t block_digest[32] = {0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8, 0xe5, 0xc0, 0x26,
                                                      0x93, 0x0c, 0x3e, 0x60, 0x39, 0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff,
                                                      0x21, 0x67, 0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1};
    uai::ai::peripheral::PeripheralManagement::Accessor accessor;
    if (!uai::ai::peripheral::PeripheralManagement::Instance().Acquire(&accessor, 100).Ok()) {
        return {Outcome::kFail, "hash-ownership"};
    }
    alignas(4) std::uint8_t digest[32]{};
    if (!accessor->Sha256({short_input, sizeof(short_input)}, 3, digest, 100, accessor.Ownership()).Ok()
        || std::memcmp(digest, short_digest, sizeof(digest)) != 0) {
        return {Outcome::kFail, "sha256-short-vector"};
    }
    context.Progress(1, 2);
    if (!accessor->Sha256({block_input, sizeof(block_input)}, 56, digest, 100, accessor.Ownership()).Ok()
        || std::memcmp(digest, block_digest, sizeof(digest)) != 0) {
        return {Outcome::kFail, "sha256-block-padding-vector"};
    }
    context.Progress(2, 2);
    return {Outcome::kPass, "sha256-two-known-answer-vectors"};
}

}