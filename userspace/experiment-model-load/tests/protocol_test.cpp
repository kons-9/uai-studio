#include "protocol.hpp"
#include <array>
#include <cstdlib>
#include <string>
void Require(bool value)
{
    if (!value) {
        std::exit(1);
    }
}
int main()
{
    std::array<std::uint8_t, 64> area{};
    experiment::model::Manifest expected{
        1,
        1,
        4,
        4,
        {0x91000000, 2, experiment::model::Crc32(reinterpret_cast<const std::uint8_t *>("AB"), 2)},
        {0x91000020, 2, experiment::model::Crc32(reinterpret_cast<const std::uint8_t *>("CD"), 2)}
    };
    experiment::model::Staging staging(
        {expected.weights.address, 16, area.data()}, {expected.blob.address, 16, area.data() + 32}, expected
    );
    std::uint32_t clock = 0;
    static std::uint32_t *time_source;
    time_source = &clock;
    experiment::model::Session session{&staging, []() {
                                           return *time_source;
                                       }};
    std::string output;
    experiment::console::Writer writer{&output, [](void *target, const char *text, std::size_t size) {
                                           static_cast<std::string *>(target)->append(text, size);
                                       }};
    const char *stat[] = {"model", "stat"};
    Require(experiment::model::Command(&session, 2, stat, writer) == experiment::console::Status::kOk);
    Require(output.find("npu=unavailable") != std::string::npos);
    const char *invalid[] = {"model", "chunk", "weights", "0", "010g"};
    Require(experiment::model::Command(&session, 5, invalid, writer) == experiment::console::Status::kInvalidArgument);
    std::uint8_t buffer[2]{};
    std::size_t size = 0;
    Require(experiment::model::Hex("aA0f", buffer, 2, size) && buffer[0] == 0xaa && buffer[1] == 15 && size == 2);
}