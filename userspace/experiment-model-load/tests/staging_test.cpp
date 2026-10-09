#include "staging.hpp"
#include <array>
#include <cstdlib>
#include <iostream>

void Require(bool condition)
{
    if (!condition) {
        std::cerr << "staging failure\n";
        std::exit(1);
    }
}
int main()
{
    std::array<std::uint8_t, 64> region{};
    const std::array<std::uint8_t, 4> weights{1, 2, 3, 4};
    const std::array<std::uint8_t, 3> blob{5, 6, 7};
    const experiment::model::Manifest expected{
        1201,
        1,
        192,
        16,
        {0x91000000, 4, experiment::model::Crc32(weights.data(), weights.size())},
        {0x91000020, 3, experiment::model::Crc32(blob.data(), blob.size())}
    };
    std::array<std::uint8_t, 52> header{'U', 'A', 'I', 'M', 1, 0, 52, 0};
    const auto store = [&header](std::size_t index, std::uint32_t value) {
        for (unsigned byte = 0; byte < 4; ++byte) {
            header[index + byte] = static_cast<std::uint8_t>(value >> (byte * 8));
        }
    };
    store(8, 1201);
    store(12, 1);
    store(16, 192);
    store(20, 16);
    store(24, expected.weights.address);
    store(28, 4);
    store(32, expected.weights.crc);
    store(36, expected.blob.address);
    store(40, 3);
    store(44, expected.blob.crc);
    store(48, experiment::model::Crc32(header.data(), 48));
    experiment::model::Staging stage({0x91000000, 16, region.data()}, {0x91000020, 16, region.data() + 32}, expected);
    Require(stage.Begin(header.data(), header.size()));
    Require(!stage.Chunk(false, 1, weights.data(), 1) && stage.Received(false) == 0);
    Require(!stage.Receiving() && !stage.Chunk(false, 0, weights.data(), 2));
    Require(!stage.Complete() && !stage.Ready());
    bool published = false;
    stage.BeforePublish(
        &published, [](void *context, const std::uint8_t *, std::size_t, const std::uint8_t *, std::size_t) {
            *static_cast<bool *>(context) = true;
            return false;
        }
    );
    Require(stage.Begin(header.data(), header.size()));
    Require(stage.Chunk(false, 0, weights.data(), 4) && stage.Chunk(true, 0, blob.data(), 3));
    Require(!stage.Complete() && published && !stage.Ready());
    stage.BeforePublish(nullptr, nullptr);
    Require(stage.Begin(header.data(), header.size()));
    Require(stage.Chunk(false, 0, weights.data(), 4));
    Require(stage.Chunk(true, 0, blob.data(), 3));
    Require(stage.Complete() && stage.Verified() && stage.Verified()->kind == 1);
    auto corrupted = header;
    corrupted[12] = 2;
    Require(!stage.Begin(corrupted.data(), corrupted.size()) && !stage.Ready());
    Require(stage.Begin(header.data(), header.size()));
    Require(stage.Chunk(false, 0, weights.data(), 4));
    const std::uint8_t wrong[3]{5, 6, 8};
    Require(stage.Chunk(true, 0, wrong, 3));
    Require(!stage.Complete() && !stage.Ready());
    experiment::model::Staging overlap({0x91000000, 16, region.data()}, {0x91000020, 16, region.data() + 4}, expected);
    Require(!overlap.Begin(header.data(), header.size()));
}