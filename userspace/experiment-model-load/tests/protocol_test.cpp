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

class FakeBackend : public experiment::model::Backend {
public:
    bool Start(const experiment::model::Manifest &) override { ++starts; return can_start; }
    experiment::model::Progress Poll() override { return progress; }
    const std::uint8_t *Output(std::size_t &bytes) override
    {
        bytes = output_bytes;
        return output.data();
    }
    bool Stop() override { ++stops; return can_stop; }
    unsigned starts = 0, stops = 0;
    bool can_start = true, can_stop = true;
    experiment::model::Progress progress = experiment::model::Progress::kRunning;
    std::size_t output_bytes = 4;
    std::array<std::uint8_t, 4> output{1, 2, 3, 4};
};

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
    FakeBackend backend;
    experiment::model::Execution execution(staging, backend);
    session.execution = &execution;
    const char *run[] = {"model", "run"};
    const char *verify[] = {"model", "verify"};
    const char *abort[] = {"model", "abort"};
    Require(experiment::model::Command(&session, 2, run, writer) == experiment::console::Status::kInvalidState);
    std::array<std::uint8_t, 52> header{'U', 'A', 'I', 'M', 1, 0, 52, 0};
    const std::array<std::uint32_t, 10> fields{
        1, 1, 4, 4, expected.weights.address, 2, expected.weights.crc,
        expected.blob.address, 2, expected.blob.crc
    };
    const auto store = [&header](std::size_t offset, std::uint32_t value) {
        for (unsigned byte = 0; byte < 4; ++byte) {
            header[offset + byte] = static_cast<std::uint8_t>(value >> (byte * 8));
        }
    };
    for (std::size_t index = 0; index < fields.size(); ++index) {
        store(8 + index * 4, fields[index]);
    }
    store(48, experiment::model::Crc32(header.data(), 48));
    const auto load = [&]() {
        Require(staging.Begin(header.data(), header.size()));
        Require(staging.Chunk(false, 0, reinterpret_cast<const std::uint8_t *>("AB"), 2));
        Require(staging.Chunk(true, 0, reinterpret_cast<const std::uint8_t *>("CD"), 2));
        Require(staging.Complete());
    };
    load();
    Require(experiment::model::Command(&session, 2, verify, writer) == experiment::console::Status::kOk);
    Require(experiment::model::Command(&session, 2, run, writer) == experiment::console::Status::kOk);
    Require(staging.InUse() && !execution.Tick(10));
    Require(experiment::model::Command(&session, 2, run, writer) == experiment::console::Status::kInvalidState);
    Require(experiment::model::Command(&session, 2, verify, writer) == experiment::console::Status::kInvalidState);
    Require(!staging.Begin(header.data(), header.size()));
    backend.progress = experiment::model::Progress::kDone;
    Require(execution.Tick(20) && !staging.InUse() && staging.Ready());
    Require(execution.Status() == experiment::model::Execution::State::kDone);
    Require(execution.Crc() == experiment::model::Crc32(backend.output.data(), backend.output.size()));
    Require(execution.Elapsed() == 20);
    Require(experiment::model::Command(&session, 2, stat, writer) == experiment::console::Status::kOk);
    Require(output.find("npu=done") != std::string::npos);
    backend.output_bytes = 3;
    Require(execution.Run(100) && execution.Tick(101));
    Require(execution.Status() == experiment::model::Execution::State::kError && execution.Crc() == 0);
    backend.output_bytes = 4;
    backend.can_start = false;
    Require(!execution.Run(200) && !staging.InUse() && staging.Ready());
    Require(execution.Status() == experiment::model::Execution::State::kError && execution.Crc() == 0);
    backend.can_start = true;
    backend.progress = experiment::model::Progress::kRunning;
    Require(execution.Run(0xfffffff0U) && !execution.Tick(5));
    Require(execution.Tick(0x1380) && !staging.InUse());
    backend.can_stop = false;
    backend.progress = experiment::model::Progress::kError;
    Require(execution.Run(0) && execution.Tick(1) && staging.InUse());
    Require(experiment::model::Command(&session, 2, abort, writer) == experiment::console::Status::kHardware);
    Require(staging.InUse());
    backend.can_stop = true;
    Require(experiment::model::Command(&session, 2, abort, writer) == experiment::console::Status::kOk);
    Require(!staging.InUse() && !staging.Ready());
    load();
    area[0] ^= 1;
    const auto starts = backend.starts;
    Require(experiment::model::Command(&session, 2, run, writer) == experiment::console::Status::kHardware);
    Require(!staging.Ready() && !staging.InUse());
    Require(backend.starts == starts && execution.Crc() == 0);
    Require(execution.Status() == experiment::model::Execution::State::kError);
    load();
    area[32] ^= 1;
    Require(experiment::model::Command(&session, 2, verify, writer) == experiment::console::Status::kHardware);
    Require(!staging.Ready());
}