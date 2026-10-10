#include "tests/framework.hpp"
#include "driver/dma2d_driver/dma2d_driver.hpp"

namespace uai::hwtest::tests::dma2d_driver {

Result Run(const Context &)
{
    namespace image = uai::ai::image_processing;
    auto &driver = uai::ai::dma2d::Dma2dManagement::Instance();
    const auto initialized = driver.Initialize();
    if (!initialized.Ok() && initialized.Code() != uai::ai::common::ErrorCode::kAlreadyInitialized) {
        return {Outcome::kFail, "dma2d-initialization"};
    }
    alignas(32) static std::uint8_t source[192];
    alignas(32) static std::uint8_t destination[192];
    for (std::size_t index = 0; index < sizeof(source); ++index) {
        source[index] = static_cast<std::uint8_t>(index * 7);
        destination[index] = 0x55;
    }
    image::Request request{};
    request.source = {source + 32, 160, 8, 8, 16, image::Format::kRgb565};
    request.destination = {destination + 32, 160, 8, 8, 16, image::Format::kRgb565};
    if (!driver.Transfer(request, 100).Ok()) {
        return {Outcome::kFail, "dma2d-transfer-or-timeout"};
    }
    for (std::size_t index = 0; index < sizeof(source); ++index) {
        const auto original = static_cast<std::uint8_t>(index * 7);
        const auto expected = index >= 32 && index < 160 ? original : 0x55;
        if (source[index] != original || destination[index] != expected) {
            return {Outcome::kFail, "dma2d-data-or-guard-mismatch"};
        }
    }
    return {Outcome::kPass, "driver-api-dma-copy-cache-guards"};
}

}
