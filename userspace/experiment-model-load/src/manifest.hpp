#pragma once

#include <cstddef>
#include <cstdint>

namespace experiment::model {

inline constexpr std::size_t kManifestBytes = 52;
struct Segment {
    std::uint32_t address, bytes, crc;
};
struct Manifest {
    std::uint32_t runtime_version, kind, input_bytes, output_bytes;
    Segment weights, blob;
};

inline std::uint32_t Crc32(
    const std::uint8_t *data,
    std::size_t size
)
{
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0U);
        }
    }
    return crc ^ 0xffffffffU;
}

inline bool ValidSegment(Segment segment)
{
    return segment.bytes != 0 && std::uint64_t(segment.address) + segment.bytes <= 0x100000000ULL;
}

inline bool Within(
    Segment segment,
    std::uint32_t base,
    std::uint32_t bytes
)
{
    return ValidSegment(segment) && std::uint64_t(base) + bytes <= 0x100000000ULL && segment.address >= base
        && std::uint64_t(segment.address) + segment.bytes <= std::uint64_t(base) + bytes;
}

inline bool Decode(
    const std::uint8_t *data,
    std::size_t size,
    Manifest &output
)
{
    if (!data || size != kManifestBytes || data[0] != 'U' || data[1] != 'A' || data[2] != 'I' || data[3] != 'M'
        || data[4] != 1 || data[5] != 0 || data[6] != kManifestBytes || data[7] != 0) {
        return false;
    }
    const auto read = [data](std::size_t offset) {
        return std::uint32_t(data[offset]) | (std::uint32_t(data[offset + 1]) << 8)
            | (std::uint32_t(data[offset + 2]) << 16) | (std::uint32_t(data[offset + 3]) << 24);
    };
    if (Crc32(data, size - 4) != read(size - 4)) {
        return false;
    }
    const Manifest value{
        read(8), read(12), read(16), read(20), {read(24), read(28), read(32)}, {read(36), read(40), read(44)}
    };
    if (value.runtime_version == 0 || value.kind == 0 || value.input_bytes == 0 || value.output_bytes == 0
        || !ValidSegment(value.weights) || !ValidSegment(value.blob)) {
        return false;
    }
    if (value.weights.address < std::uint64_t(value.blob.address) + value.blob.bytes
        && value.blob.address < std::uint64_t(value.weights.address) + value.weights.bytes) {
        return false;
    }
    output = value;
    return true;
}

inline bool Matches(
    const Manifest &actual,
    const Manifest &expected
)
{
    return actual.runtime_version == expected.runtime_version && actual.kind == expected.kind
        && actual.input_bytes == expected.input_bytes && actual.output_bytes == expected.output_bytes
        && actual.weights.address == expected.weights.address && actual.weights.bytes == expected.weights.bytes
        && actual.weights.crc == expected.weights.crc && actual.blob.address == expected.blob.address
        && actual.blob.bytes == expected.blob.bytes && actual.blob.crc == expected.blob.crc;
}

inline bool Verify(
    const std::uint8_t *data,
    std::size_t size,
    Segment segment
)
{
    return data && size == segment.bytes && Crc32(data, size) == segment.crc;
}

}