#include "manifest.hpp"
#include <fstream>
#include <iterator>
#include <vector>

std::vector<std::uint8_t> Read(const char *path)
{
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

int main(int count, char **arguments)
{
    if (count != 4) { return 2; }
    const auto data = Read(arguments[1]);
    const auto weights = Read(arguments[2]);
    const auto blob = Read(arguments[3]);
    experiment::model::Manifest manifest{};
    if (!experiment::model::Decode(data.data(), data.size(), manifest)) { return 1; }
    if (!experiment::model::Within(manifest.weights, 0x91000000, 0x10000) ||
        !experiment::model::Within(manifest.blob, 0x91000000, 0x10000)) { return 1; }
    if (!experiment::model::Verify(weights.data(), weights.size(), manifest.weights) ||
        !experiment::model::Verify(blob.data(), blob.size(), manifest.blob)) { return 1; }
    const experiment::model::Manifest expected{1201, 1, 192, 16,
        {0x91000000, 16, experiment::model::Crc32(weights.data(), weights.size())},
        {0x91000100, 8, experiment::model::Crc32(blob.data(), blob.size())}};
    return experiment::model::Matches(manifest, expected) ? 0 : 1;
}