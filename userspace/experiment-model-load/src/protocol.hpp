#pragma once
#include "execution.hpp"
#include "shell.hpp"
#include "camera_control.hpp"
#include <cstdio>

namespace experiment::model {

struct Session {
    Staging *staging;
    std::uint32_t (*clock)();
    std::uint32_t last_input = 0;
    std::uint32_t timeout_ms = 10000;
    Execution *execution = nullptr;
};

inline bool
Hex(const char *text,
    std::uint8_t *buffer,
    std::size_t capacity,
    std::size_t &length)
{
    length = std::strlen(text);
    if (length == 0 || length % 2 || length / 2 > capacity) {
        return false;
    }
    length /= 2;
    const auto digit = [](char value) -> int {
        if (value >= '0' && value <= '9') {
            return value - '0';
        }
        if (value >= 'a' && value <= 'f') {
            return value - 'a' + 10;
        }
        if (value >= 'A' && value <= 'F') {
            return value - 'A' + 10;
        }
        return -1;
    };
    for (std::size_t index = 0; index < length; ++index) {
        const auto high = digit(text[index * 2]), low = digit(text[index * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        buffer[index] = static_cast<std::uint8_t>(high * 16 + low);
    }
    return true;
}

inline console::Status Command(
    void *context,
    int count,
    const char *const *arguments,
    const console::Writer &writer
)
{
    auto &session = *static_cast<Session *>(context);
    if (!session.staging || !session.clock) {
        return console::Status::kInvalidState;
    }
    auto &stage = *session.staging;
    if (stage.Receiving() && session.clock() - session.last_input > session.timeout_ms) {
        stage.Cancel();
    }
    if (count == 2 && std::strcmp(arguments[1], "stat") == 0) {
        char line[192];
        std::snprintf(
            line,
            sizeof(line),
            "MODEL state=%s weights=%lu blob=%lu npu=%s output_crc=%08lx elapsed_ms=%lu\n",
            stage.Ready()           ? "verified"
                : stage.Receiving() ? "receiving"
                                    : "empty",
            static_cast<unsigned long>(stage.Received(false)),
            static_cast<unsigned long>(stage.Received(true)),
            session.execution ? session.execution->Name() : "unavailable",
            static_cast<unsigned long>(session.execution ? session.execution->Crc() : 0),
            static_cast<unsigned long>(session.execution ? session.execution->Elapsed() : 0)
        );
        writer.Write(line);
        return console::Status::kOk;
    }
    if (count == 2 && std::strcmp(arguments[1], "abort") == 0) {
        if (session.execution && !session.execution->Reset()) {
            return console::Status::kHardware;
        }
        if (!stage.Cancel()) {
            return console::Status::kInvalidState;
        }
        writer.Write("MODEL OK abort\n");
        return console::Status::kOk;
    }
    if (count == 2 && std::strcmp(arguments[1], "verify") == 0) {
        if (!stage.Ready() || stage.InUse()) {
            return console::Status::kInvalidState;
        }
        if (!stage.Reverify()) {
            if (session.execution) {
                session.execution->Reset();
            }
            return console::Status::kHardware;
        }
        writer.Write("MODEL OK verified\n");
        return console::Status::kOk;
    }
    if (count == 2 && std::strcmp(arguments[1], "run") == 0) {
        if (!session.execution || !stage.Ready() || stage.InUse()) {
            return console::Status::kInvalidState;
        }
        if (!session.execution->Run(session.clock())) {
            return console::Status::kHardware;
        }
        writer.Write("MODEL OK running\n");
        return console::Status::kOk;
    }
    std::uint8_t bytes[52]{};
    std::size_t length = 0;
    if (count == 3 && std::strcmp(arguments[1], "begin") == 0) {
        if (stage.InUse()) {
            return console::Status::kInvalidState;
        }
        if (session.execution && !session.execution->Reset()) {
            return console::Status::kHardware;
        }
        if (!Hex(arguments[2], bytes, sizeof(bytes), length) || length != kManifestBytes
            || !stage.Begin(bytes, length)) {
            return console::Status::kInvalidArgument;
        }
    } else if (count == 5 && std::strcmp(arguments[1], "chunk") == 0) {
        const bool blob = std::strcmp(arguments[2], "blob") == 0;
        if (!blob && std::strcmp(arguments[2], "weights") != 0) {
            return console::Status::kInvalidArgument;
        }
        std::int32_t offset = 0;
        if (!camera::ParseInteger(arguments[3], offset) || offset < 0 || !Hex(arguments[4], bytes, 32, length)
            || !stage.Chunk(blob, static_cast<std::uint32_t>(offset), bytes, length)) {
            return console::Status::kInvalidArgument;
        }
    } else if (count == 2 && std::strcmp(arguments[1], "commit") == 0) {
        if (!stage.Complete()) {
            return console::Status::kHardware;
        }
    } else {
        return console::Status::kInvalidArgument;
    }
    session.last_input = session.clock();
    writer.Write("MODEL OK\n");
    return console::Status::kOk;
}

}