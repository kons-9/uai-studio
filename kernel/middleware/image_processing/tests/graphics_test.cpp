#include "middleware/image_processing/operations.hpp"
#include "middleware/image_processing/verification.hpp"

#include <gtest/gtest.h>

#include <array>
#include <limits>

namespace {

namespace graphics = uai::ai::image_processing;

TEST(ImageTransfer, ValidatesCapacityStrideFormatAndAddressOverflow)
{
    std::array<std::uint8_t, 32> storage{};
    graphics::Image image{storage.data(), 14, 2, 2, 8, graphics::Format::kRgb888};
    EXPECT_FALSE(graphics::Valid(image));
    image.stride = 9;
    EXPECT_FALSE(graphics::Valid(image));
    image.bytes = 15;
    EXPECT_TRUE(graphics::Valid(image));
    image.bytes = 14;
    EXPECT_FALSE(graphics::Valid(image));
    image.bytes = storage.size();
    image.width = 4;
    EXPECT_FALSE(graphics::Valid(image));
    image.width = 2;
    image.format = static_cast<graphics::Format>(99);
    EXPECT_FALSE(graphics::Valid(image));
    image.format = graphics::Format::kRgb888;
    image.data = reinterpret_cast<std::uint8_t *>(std::numeric_limits<std::uintptr_t>::max() - 8);
    EXPECT_FALSE(graphics::Valid(image));
    EXPECT_FALSE(graphics::Disjoint(image, image));
    image.data = nullptr;
    EXPECT_FALSE(graphics::Valid(image));
}

TEST(ImageTransfer, BuildsPixelOffsetsAndRejectsOverlappingRanges)
{
    std::array<std::uint8_t, 64> storage{};
    graphics::Image source{storage.data(), 16, 2, 2, 8, graphics::Format::kRgb565};
    graphics::Image destination{storage.data() + 16, 18, 2, 2, 9, graphics::Format::kRgb888};
    graphics::Transfer transfer{};
    ASSERT_TRUE(graphics::BuildTransfer(source, destination, transfer));
    EXPECT_EQ(transfer.input_offset, 2U);
    EXPECT_EQ(transfer.output_offset, 1U);
    EXPECT_EQ(transfer.mode, graphics::Mode::kConvert);
    destination.data = storage.data() + 15;
    EXPECT_FALSE(graphics::BuildTransfer(source, destination, transfer));
    destination.data = storage.data() + 16;
    destination.width = 1;
    EXPECT_FALSE(graphics::BuildTransfer(source, destination, transfer));
}

TEST(ImageOperations, ConvertsBothFormatsAndPreservesPaddingAndGuards)
{
    const std::array<std::uint8_t, 12> original{255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
    auto source_pixels = original;
    std::array<std::uint8_t, 16> destination_pixels;
    destination_pixels.fill(0xa5);
    graphics::Image source{source_pixels.data(), source_pixels.size(), 2, 2, 6, graphics::Format::kRgb888};
    graphics::Image destination{destination_pixels.data() + 2, 10, 2, 2, 6, graphics::Format::kRgb565};
    ASSERT_TRUE(graphics::ReferenceBlit(source, destination));
    EXPECT_EQ(graphics::ReadPixel(destination, 0, 0), 0xff0000U);
    EXPECT_EQ(graphics::ReadPixel(destination, 1, 0), 0x00ff00U);
    EXPECT_EQ(graphics::ReadPixel(destination, 0, 1), 0x0000ffU);
    EXPECT_EQ(graphics::ReadPixel(destination, 1, 1), 0xffffffU);
    for (const auto index : {0U, 1U, 6U, 7U, 12U, 13U, 14U, 15U}) {
        EXPECT_EQ(destination_pixels[index], 0xa5);
    }
    EXPECT_EQ(source_pixels, original);
    std::array<std::uint8_t, 12> roundtrip{};
    graphics::Image converted{roundtrip.data(), roundtrip.size(), 2, 2, 6, graphics::Format::kRgb888};
    ASSERT_TRUE(graphics::ReferenceBlit(destination, converted));
    EXPECT_EQ(roundtrip, original);
}

TEST(ImageOperations, RejectsInvalidRequestsWithoutWriting)
{
    std::array<std::uint8_t, 32> source_pixels{};
    std::array<std::uint8_t, 32> destination_pixels;
    destination_pixels.fill(0xa5);
    const auto original = destination_pixels;
    graphics::Request request{graphics::Operation::kBlit,
        {source_pixels.data(), source_pixels.size(), 2, 2, 6, graphics::Format::kRgb888}, {},
        {destination_pixels.data(), 11, 2, 2, 6, graphics::Format::kRgb888}};
    EXPECT_FALSE(graphics::Reference(request));
    EXPECT_EQ(destination_pixels, original);
    request.destination.bytes = destination_pixels.size();
    request.destination.stride = 5;
    EXPECT_FALSE(graphics::Reference(request));
    EXPECT_EQ(destination_pixels, original);
    request.destination.stride = 6;
    request.operation = graphics::Operation::kBlend;
    request.background = request.destination;
    EXPECT_FALSE(graphics::Reference(request));
    EXPECT_EQ(destination_pixels, original);
    request.operation = static_cast<graphics::Operation>(99);
    EXPECT_FALSE(graphics::Reference(request));
    EXPECT_EQ(destination_pixels, original);
    request.operation = graphics::Operation::kFill;
    request.color = 0x1000000;
    EXPECT_FALSE(graphics::Reference(request));
    EXPECT_EQ(destination_pixels, original);
}

TEST(ImageOperations, FillsAndBlendsAtAlphaEndpointsAndMidpoint)
{
    std::array<std::uint8_t, 3> foreground{255, 0, 0};
    std::array<std::uint8_t, 3> background{0, 0, 255};
    std::array<std::uint8_t, 3> destination{};
    graphics::Request request{graphics::Operation::kFill, {}, {},
        {destination.data(), destination.size(), 1, 1, 3, graphics::Format::kRgb888}, 0x12ab34};
    ASSERT_TRUE(graphics::Reference(request));
    EXPECT_EQ(graphics::ReadPixel(request.destination, 0, 0), 0x12ab34U);
    request.operation = graphics::Operation::kBlend;
    request.source = {foreground.data(), foreground.size(), 1, 1, 3, graphics::Format::kRgb888};
    request.background = {background.data(), background.size(), 1, 1, 3, graphics::Format::kRgb888};
    request.alpha = 0;
    ASSERT_TRUE(graphics::Reference(request));
    EXPECT_EQ(destination, background);
    request.alpha = 255;
    ASSERT_TRUE(graphics::Reference(request));
    EXPECT_EQ(destination, foreground);
    request.alpha = 128;
    ASSERT_TRUE(graphics::Reference(request));
    EXPECT_EQ(graphics::ReadPixel(request.destination, 0, 0), 0x80007fU);
}

TEST(ImageOperations, ResizeUsesPixelCenters)
{
    std::array<std::uint8_t, 12> source{255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
    std::array<std::uint8_t, 3> destination{};
    graphics::Request request{graphics::Operation::kResize,
        {source.data(), source.size(), 2, 2, 6, graphics::Format::kRgb888}, {},
        {destination.data(), destination.size(), 1, 1, 3, graphics::Format::kRgb888}};
    ASSERT_TRUE(graphics::Reference(request));
    EXPECT_EQ(graphics::ReadPixel(request.destination, 0, 0), 0xffffffU);
}

struct ReferenceDevice {
    bool Run(const graphics::Request &request, std::uint32_t timeout)
    {
        EXPECT_EQ(timeout, 100U);
        if (request.operation == graphics::Operation::kResize
            || reinterpret_cast<std::uintptr_t>(request.destination.data) % 4 != 0) {
            return false;
        }
        return graphics::Reference(request);
    }
};

TEST(ImageVerification, RunsAllHardwareCasesAndHandlesClockWrap)
{
    graphics::Verification verification;
    ReferenceDevice device;
    for (const auto &test : graphics::kVerificationCases) {
        SCOPED_TRACE(test.name);
        std::uint32_t cycles = std::numeric_limits<std::uint32_t>::max() - 5;
        const auto result = verification.Run(test, device, [&cycles] {
            const auto value = cycles;
            cycles += 10;
            return value;
        });
        EXPECT_TRUE(result.passed);
        EXPECT_EQ(result.cycles, 10U);
        EXPECT_EQ(result.corrupted_bytes, 0U);
        EXPECT_EQ(result.maximum_error, 0U);
    }
}

struct CorruptingDevice {
    enum class Fault { kPixel, kInput, kBackground, kGuard, kPadding, kFalseSuccess, kFalseFailure };
    Fault fault;

    bool Run(const graphics::Request &request, std::uint32_t)
    {
        if (fault == Fault::kFalseSuccess) {
            request.destination.data[0] ^= 1;
            return false;
        }
        if (!graphics::Reference(request)) {
            return false;
        }
        switch (fault) {
        case Fault::kPixel:
            request.destination.data[0] ^= 0x80;
            break;
        case Fault::kInput:
            request.source.data[0] ^= 1;
            break;
        case Fault::kBackground:
            request.background.data[0] ^= 1;
            break;
        case Fault::kGuard:
            request.destination.data[-1] ^= 1;
            break;
        case Fault::kPadding:
            request.destination.data[request.destination.width * graphics::PixelBytes(request.destination.format)] ^= 1;
            break;
        case Fault::kFalseFailure:
            return false;
        case Fault::kFalseSuccess:
            break;
        }
        return true;
    }
};

TEST(ImageVerification, DetectsPixelInputBackgroundGuardAndPaddingCorruption)
{
    graphics::Verification verification;
    const graphics::VerificationCase test{"padded-blend", graphics::Operation::kBlend,
        graphics::Format::kRgb888, graphics::Format::kRgb888, 128, 0, true};
    for (const auto fault : {CorruptingDevice::Fault::kPixel, CorruptingDevice::Fault::kInput,
             CorruptingDevice::Fault::kBackground, CorruptingDevice::Fault::kGuard,
             CorruptingDevice::Fault::kPadding, CorruptingDevice::Fault::kFalseFailure}) {
        SCOPED_TRACE(static_cast<int>(fault));
        CorruptingDevice device{fault};
        const auto result = verification.Run(test, device, [] { return 0U; });
        EXPECT_FALSE(result.passed);
        if (fault == CorruptingDevice::Fault::kPixel) {
            EXPECT_GT(result.maximum_error, 1U);
        } else if (fault != CorruptingDevice::Fault::kFalseFailure) {
            EXPECT_GT(result.corrupted_bytes, 0U);
        }
    }
}

TEST(ImageVerification, RejectingAfterWritingStillFails)
{
    graphics::Verification verification;
    CorruptingDevice device{CorruptingDevice::Fault::kFalseSuccess};
    const graphics::VerificationCase test{"short", graphics::Operation::kBlit,
        graphics::Format::kRgb888, graphics::Format::kRgb888,
        255, 0, false, graphics::Rejection::kShortBuffer};
    const auto result = verification.Run(test, device, [] { return 0U; });
    EXPECT_FALSE(result.passed);
    EXPECT_GT(result.corrupted_bytes, 0U);
}

TEST(ImageVerification, CacheHooksCoverAlignedBuffersBeforeAndAfterTransfer)
{
    static unsigned stage;
    stage = 0;
    const graphics::VerificationCache cache{
        [](void *address, std::int32_t bytes) {
            EXPECT_LT(stage, 3U);
            EXPECT_EQ(reinterpret_cast<std::uintptr_t>(address) % 32, 0U);
            EXPECT_EQ(bytes % 32, 0);
            ++stage;
        },
        [](void *, std::int32_t) {
            EXPECT_GE(stage, 4U);
            EXPECT_LT(stage, 7U);
            ++stage;
        }
    };
    struct Device {
        bool Run(const graphics::Request &request, std::uint32_t)
        {
            EXPECT_EQ(stage, 3U);
            ++stage;
            return graphics::Reference(request);
        }
    } device;
    graphics::Verification verification;
    EXPECT_TRUE(verification.Run(graphics::kVerificationCases[0], device, [] { return 0U; }, cache).passed);
    EXPECT_EQ(stage, 7U);
}

}