#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>

namespace experiment::model {

inline constexpr std::size_t kManifestBytes = 52;
inline constexpr std::size_t kHeaderMaxBytes = 448;
inline constexpr std::size_t kMaxOutputs = 8;
struct Tensor {
    std::uint8_t type = 0, layout = 0, rank = 0, role = 0;
    std::uint32_t shape[4]{}, bytes = 0;
    float scale = 1;
    std::int32_t zero = 0;
    std::uint32_t offset = 0;
};
struct Segment {
    std::uint32_t address, bytes, crc;
};
struct Manifest {
    std::uint32_t runtime_version, kind, input_bytes, output_bytes;
    Segment weights, blob;
    std::uint32_t tag = 0;
    std::uint8_t output_count = 0, preprocessing = 0, color = 0;
    std::uint32_t padding = 0;
    float mean[3]{}, divisor[3]{1, 1, 1};
    Tensor input{}, outputs[kMaxOutputs]{};
};

inline bool ValidTensor(const Tensor &tensor)
{
    constexpr std::uint32_t widths[] = {0, 1, 1, 4, 2, 2, 2, 4};
    if (tensor.type == 0 || tensor.type > 7 || tensor.layout > 2 || tensor.rank == 0 || tensor.rank > 4
        || !std::isfinite(tensor.scale) || tensor.scale <= 0 || tensor.offset % 32) {
        return false;
    }
    std::uint64_t bytes = widths[tensor.type];
    for (unsigned index = 0; index < 4; ++index) {
        if (index < tensor.rank) {
            if (!tensor.shape[index] || bytes > UINT32_MAX / tensor.shape[index]) {
                return false;
            }
            bytes *= tensor.shape[index];
        } else if (tensor.shape[index]) {
            return false;
        }
    }
    if ((tensor.type == 1 && (tensor.zero < 0 || tensor.zero > 255))
        || (tensor.type == 2 && (tensor.zero < -128 || tensor.zero > 127))
        || (tensor.type == 4 && (tensor.zero < -32768 || tensor.zero > 32767))
        || (tensor.type == 5 && (tensor.zero < 0 || tensor.zero > 65535))
        || ((tensor.type == 3 || tensor.type == 6) && tensor.zero != 0)) {
        return false;
    }
    return bytes == tensor.bytes;
}

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
    if (!data || size < kManifestBytes || size > kHeaderMaxBytes
        || data[0] != 'U' || data[1] != 'A' || data[2] != 'I' || data[3] != 'M'
        || (data[4] != 1 && data[4] != 2) || data[5] != 0
        || (std::size_t(data[6]) | (std::size_t(data[7]) << 8)) != size) {
        return false;
    }
    const auto read = [data](std::size_t offset) {
        return std::uint32_t(data[offset]) | (std::uint32_t(data[offset + 1]) << 8)
            | (std::uint32_t(data[offset + 2]) << 16) | (std::uint32_t(data[offset + 3]) << 24);
    };
    if (Crc32(data, size - 4) != read(size - 4)) {
        return false;
    }
    Manifest value{
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
    if (data[4] == 1) {
        if (size != kManifestBytes) {
            return false;
        }
    } else {
        if (size < 128 || data[52] != 1 || data[53] == 0 || data[53] > kMaxOutputs
            || size != 88U + (1U + data[53]) * 40U || read(56) > 255 || read(48) == 0
            || data[54] > 2 || data[55] > 3) {
            return false;
        }
        const auto real = [&read](std::size_t offset) {
            const auto bits = read(offset);
            float result;
            std::memcpy(&result, &bits, sizeof(result));
            return result;
        };
        value.tag = read(48);
        value.output_count = data[53];
        value.preprocessing = data[54];
        value.color = data[55];
        value.padding = read(56);
        for (unsigned index = 0; index < 3; ++index) {
            value.mean[index] = real(60 + index * 4);
            value.divisor[index] = real(72 + index * 4);
            if (!std::isfinite(value.mean[index]) || !std::isfinite(value.divisor[index]) || value.divisor[index] <= 0) {
                return false;
            }
        }
        std::uint64_t end = 0;
        for (unsigned index = 0; index <= value.output_count; ++index) {
            const auto offset = 84 + index * 40;
            auto &tensor = index == 0 ? value.input : value.outputs[index - 1];
            tensor.type = data[offset]; tensor.layout = data[offset + 1];
            tensor.rank = data[offset + 2]; tensor.role = data[offset + 3];
            for (unsigned axis = 0; axis < 4; ++axis) {
                tensor.shape[axis] = read(offset + 4 + axis * 4);
            }
            tensor.bytes = read(offset + 20);
            tensor.scale = real(offset + 24);
            const auto zero = read(offset + 28);
            std::memcpy(&tensor.zero, &zero, sizeof(zero));
            tensor.offset = read(offset + 32);
            if (read(offset + 36) != 0 || !ValidTensor(tensor)) {
                return false;
            }
            if (index == 0) {
                if (tensor.offset || tensor.bytes != value.input_bytes) {
                    return false;
                }
            } else {
                if (tensor.offset < end) {
                    return false;
                }
                end = std::uint64_t(tensor.offset) + tensor.bytes;
            }
        }
        if (end != value.output_bytes) {
            return false;
        }
        if (value.preprocessing) {
            const auto &tensor = value.input;
            if (tensor.rank != 4 || tensor.shape[0] != 1 || tensor.layout == 0 || !value.color
                || tensor.shape[tensor.layout == 1 ? 3 : 1] != (value.color == 3 ? 1U : 3U)) {
                return false;
            }
        }
    }
    output = value;
    return true;
}

inline bool Matches(
    const Manifest &actual,
    const Manifest &expected
)
{
    if (actual.tag != expected.tag || actual.output_count != expected.output_count
        || actual.preprocessing != expected.preprocessing || actual.color != expected.color || actual.padding != expected.padding) {
        return false;
    }
    if (actual.tag) {
        for (unsigned index = 0; index < 3; ++index) {
            if (actual.mean[index] != expected.mean[index] || actual.divisor[index] != expected.divisor[index]) {
                return false;
            }
        }
        for (unsigned index = 0; index <= actual.output_count; ++index) {
            const auto &left = index ? actual.outputs[index - 1] : actual.input;
            const auto &right = index ? expected.outputs[index - 1] : expected.input;
            if (left.type != right.type || left.layout != right.layout || left.rank != right.rank || left.role != right.role
                || left.bytes != right.bytes || left.scale != right.scale || left.zero != right.zero || left.offset != right.offset) {
                return false;
            }
            for (unsigned axis = 0; axis < 4; ++axis) {
                if (left.shape[axis] != right.shape[axis]) {
                    return false;
                }
            }
        }
    }
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