#include "manifest.hpp"
#include <fstream>
#include <iterator>
#include <vector>
#include <cstdio>

std::vector<std::uint8_t> Read(const char *path)
{
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

int main(
    int count,
    char **arguments
)
{
    if (count == 3) {
        const auto data = Read(arguments[2]);
        experiment::model::Manifest value{};
        if (!experiment::model::Decode(data.data(), data.size(), value)) {
            return 1;
        }
        std::printf("%u %u %u %u %u\n", value.tag, value.input.type, value.input_bytes, value.output_count, value.output_bytes);
        return 0;
    }
    if (count != 4) {
        return 2;
    }
    const auto data = Read(arguments[1]);
    const auto weights = Read(arguments[2]);
    const auto blob = Read(arguments[3]);
    experiment::model::Manifest manifest{};
    if (!experiment::model::Decode(data.data(), data.size(), manifest)) {
        return 1;
    }
    if (!experiment::model::Within(manifest.weights, 0x91200000, 0x800000)
        || !experiment::model::Within(manifest.blob, 0x91a00000, 0x200000)) {
        return 1;
    }
    if (!experiment::model::Verify(weights.data(), weights.size(), manifest.weights)
        || !experiment::model::Verify(blob.data(), blob.size(), manifest.blob)) {
        return 1;
    }
    const experiment::model::Manifest expected{
        1201,
        1,
        192,
        16,
        {0x91200000, 16, experiment::model::Crc32(weights.data(), weights.size())},
        {0x91a00000, 8, experiment::model::Crc32(blob.data(), blob.size())}
    };
    return experiment::model::Matches(manifest, expected) ? 0 : 1;
}
