#pragma once

#include "operations.hpp"

#include <cstring>

namespace uai::ai::image_processing {

enum class Rejection {
    kNone,
    kOverlap,
    kBackgroundOverlap,
    kShortBuffer,
    kStride,
    kUnaligned,
    kResize
};

struct VerificationCase {
    const char *name;
    Operation operation;
    Format source;
    Format destination;
    std::uint8_t alpha = 255;
    std::uint32_t color = 0xc02070;
    bool padded = false;
    Rejection rejection = Rejection::kNone;
};

inline constexpr VerificationCase kVerificationCases[] = {
    {"copy-rgb888", Operation::kBlit, Format::kRgb888, Format::kRgb888},
    {"copy-rgb565", Operation::kBlit, Format::kRgb565, Format::kRgb565},
    {"copy-padded", Operation::kBlit, Format::kRgb888, Format::kRgb888, 255, 0, true},
    {"convert-888-to-565", Operation::kBlit, Format::kRgb888, Format::kRgb565},
    {"convert-565-to-888", Operation::kBlit, Format::kRgb565, Format::kRgb888},
    {"convert-padded", Operation::kBlit, Format::kRgb888, Format::kRgb565, 255, 0, true},
    {"fill-565-red", Operation::kFill, Format::kRgb888, Format::kRgb565, 255, 0xff0000},
    {"fill-565-green", Operation::kFill, Format::kRgb888, Format::kRgb565, 255, 0x00ff00},
    {"fill-565-blue", Operation::kFill, Format::kRgb888, Format::kRgb565, 255, 0x0000ff},
    {"fill-888-red", Operation::kFill, Format::kRgb888, Format::kRgb888, 255, 0xff0000},
    {"fill-888-green", Operation::kFill, Format::kRgb888, Format::kRgb888, 255, 0x00ff00},
    {"fill-888-blue", Operation::kFill, Format::kRgb888, Format::kRgb888, 255, 0x0000ff},
    {"fill-padded", Operation::kFill, Format::kRgb888, Format::kRgb565, 255, 0xc02070, true},
    {"blend-565-alpha0", Operation::kBlend, Format::kRgb888, Format::kRgb565, 0},
    {"blend-565-alpha128", Operation::kBlend, Format::kRgb888, Format::kRgb565, 128},
    {"blend-565-alpha255", Operation::kBlend, Format::kRgb888, Format::kRgb565, 255},
    {"blend-888-alpha128", Operation::kBlend, Format::kRgb888, Format::kRgb888, 128},
    {"blend-padded", Operation::kBlend, Format::kRgb565, Format::kRgb888, 128, 0, true},
    {"reject-overlap", Operation::kBlit, Format::kRgb888, Format::kRgb888, 255, 0, false, Rejection::kOverlap},
    {"reject-background-overlap",
     Operation::kBlend,
     Format::kRgb888,
     Format::kRgb888,
     128,
     0,
     false,
     Rejection::kBackgroundOverlap},
    {"reject-short-buffer", Operation::kBlit, Format::kRgb888, Format::kRgb888, 255, 0, false, Rejection::kShortBuffer},
    {"reject-stride", Operation::kBlit, Format::kRgb888, Format::kRgb888, 255, 0, false, Rejection::kStride},
    {"reject-unaligned", Operation::kBlit, Format::kRgb888, Format::kRgb888, 255, 0, false, Rejection::kUnaligned},
    {"reject-resize", Operation::kResize, Format::kRgb888, Format::kRgb565, 255, 0, false, Rejection::kResize},
    {"reuse-after-rejection", Operation::kBlit, Format::kRgb888, Format::kRgb565}
};

struct VerificationResult {
    bool passed = false;
    std::uint32_t cycles = 0;
    unsigned maximum_error = 0;
    unsigned corrupted_bytes = 0;
};

struct VerificationCache {
    void (*prepare)(
        void *,
        std::int32_t
    ) = nullptr;
    void (*inspect)(
        void *,
        std::int32_t
    ) = nullptr;
};

class Verification {
public:
    template <
        typename Device,
        typename Clock>
    VerificationResult
    Run(const VerificationCase &test,
        Device &device,
        Clock clock,
        VerificationCache cache = {})
    {
        ++generation_;
        Initialize(source_, generation_);
        Initialize(background_, 255 - generation_);
        std::memset(actual_.storage, 0xa5, sizeof(actual_.storage));
        std::memset(expected_.storage, 0xa5, sizeof(expected_.storage));
        const std::uint32_t width = test.padded ? 63 : 64;
        const std::uint32_t height = test.padded ? 31 : 32;
        Request request{
            test.operation,
            {source_.Data(), kBytes, width, height, 64 * PixelBytes(test.source), test.source},
            {background_.Data(), kBytes, width, height, 64 * PixelBytes(test.source), test.source},
            {expected_.Data(), kBytes, width, height, 64 * PixelBytes(test.destination), test.destination},
            test.color,
            test.alpha
        };
        const bool rejected = test.rejection != Rejection::kNone;
        if (!rejected && !Reference(request)) {
            return {};
        }
        request.destination.data = actual_.Data();
        switch (test.rejection) {
        case Rejection::kOverlap:
            request.destination = request.source;
            break;
        case Rejection::kBackgroundOverlap:
            request.background = request.destination;
            break;
        case Rejection::kShortBuffer:
            request.destination.bytes = 32;
            break;
        case Rejection::kStride:
            request.destination.stride = width * PixelBytes(test.destination) - 1;
            break;
        case Rejection::kUnaligned:
            ++request.destination.data;
            break;
        case Rejection::kResize:
            request.destination.width = 32;
            request.destination.height = 16;
            break;
        case Rejection::kNone:
            break;
        }
        Buffer *const buffers[] = {&source_, &background_, &actual_};
        if (cache.prepare) {
            for (auto *buffer : buffers) {
                cache.prepare(buffer->storage, sizeof(buffer->storage));
            }
        }
        const auto begin = clock();
        const bool success = device.Run(request, 100);
        VerificationResult result{success != rejected, static_cast<std::uint32_t>(clock() - begin), 0, 0};
        if (cache.inspect) {
            for (auto *buffer : buffers) {
                cache.inspect(buffer->storage, sizeof(buffer->storage));
            }
        }
        const bool approximate =
            !rejected && test.operation == Operation::kBlend && test.alpha != 0 && test.alpha != 255;
        for (std::size_t index = 0; index < sizeof(actual_.storage); ++index) {
            const bool pixel = index >= 32 && index < 32 + height * request.destination.stride
                && (index - 32) % request.destination.stride < width * PixelBytes(test.destination);
            if ((!approximate || !pixel) && actual_.storage[index] != expected_.storage[index]) {
                ++result.corrupted_bytes;
            }
            if (source_.storage[index] != Pattern(index, generation_)
                || background_.storage[index] != Pattern(index, 255 - generation_)) {
                ++result.corrupted_bytes;
            }
        }
        if (!rejected) {
            auto reference = request.destination;
            reference.data = expected_.Data();
            for (std::uint32_t row = 0; row < height; ++row) {
                for (std::uint32_t column = 0; column < width; ++column) {
                    const auto wanted = ReadPixel(reference, column, row);
                    const auto actual = ReadPixel(request.destination, column, row);
                    for (unsigned shift = 0; shift <= 16; shift += 8) {
                        const int difference = int((actual >> shift) & 255) - int((wanted >> shift) & 255);
                        const auto error = static_cast<unsigned>(difference < 0 ? -difference : difference);
                        if (error > result.maximum_error) {
                            result.maximum_error = error;
                        }
                    }
                }
            }
        }
        const unsigned tolerance = approximate ? (test.destination == Format::kRgb565 ? 8 : 1) : 0;
        result.passed = result.passed && result.corrupted_bytes == 0 && result.maximum_error <= tolerance;
        return result;
    }

private:
    static constexpr std::size_t kBytes = 64 * 32 * 3;

    struct alignas(32) Buffer {
        std::uint8_t storage[kBytes + 64];
        std::uint8_t *Data() { return storage + 32; }
    };

    static std::uint8_t Pattern(
        std::size_t index,
        std::uint32_t seed
    )
    {
        return index < 32 || index >= kBytes + 32 ? 0x5a : static_cast<std::uint8_t>((index - 32) * 37 + seed);
    }

    static void Initialize(
        Buffer &buffer,
        std::uint32_t seed
    )
    {
        for (std::size_t index = 0; index < sizeof(buffer.storage); ++index) {
            buffer.storage[index] = Pattern(index, seed);
        }
    }

    Buffer source_{}, background_{}, actual_{}, expected_{};
    std::uint32_t generation_ = 0;
};

}