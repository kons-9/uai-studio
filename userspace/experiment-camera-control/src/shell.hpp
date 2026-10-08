#pragma once

#include <cstddef>
#include <cstring>

namespace experiment::console {

enum class Status { kOk, kUnknownCommand, kInvalidArgument, kLineTooLong, kTooManyArguments, kHardware, kInvalidState };

struct Writer {
    void *context;
    void (*write)(void *, const char *, std::size_t);

    void Write(const char *text) const { write(context, text, std::strlen(text)); }
};

struct Command {
    const char *name;
    const char *help;
    Status (*execute)(void *, int, const char *const *, const Writer &);
    void *context;
};

inline const char *StatusName(Status status)
{
    switch (status) {
    case Status::kOk: return "ok";
    case Status::kUnknownCommand: return "unknown-command";
    case Status::kInvalidArgument: return "invalid-argument";
    case Status::kLineTooLong: return "line-too-long";
    case Status::kTooManyArguments: return "too-many-arguments";
    case Status::kHardware: return "hardware";
    case Status::kInvalidState: return "invalid-state";
    }
    return "invalid-status";
}

class Shell {
public:
    static constexpr std::size_t kLineCapacity = 128;
    static constexpr std::size_t kMaxArguments = 8;

    Shell(const Command *commands, std::size_t count, Writer writer)
        : commands_(commands), count_(count), writer_(writer) {}

    Status Feed(char character)
    {
        if (character == '\n' && after_cr_) {
            after_cr_ = false;
            return Status::kOk;
        }
        after_cr_ = character == '\r';
        if (character == '\r' || character == '\n') {
            writer_.Write("\n");
            line_[length_] = '\0';
            const Status result = pending_ == Status::kOk ? Execute() : pending_;
            length_ = 0;
            pending_ = Status::kOk;
            if (result != Status::kOk) {
                writer_.Write("ERR ");
                writer_.Write(StatusName(result));
                writer_.Write("\n");
            }
            writer_.Write("> ");
            return result;
        }
        if (character == '\x03') {
            length_ = 0;
            pending_ = Status::kOk;
            writer_.Write("^C\n> ");
            return Status::kOk;
        }
        if (pending_ != Status::kOk) {
            return pending_;
        }
        if (character == '\b' || character == '\x7f') {
            if (length_ != 0) {
                --length_;
                writer_.Write("\b \b");
            }
            return Status::kOk;
        }
        if ((character < ' ' && character != '\t') || character > '~') {
            pending_ = Status::kInvalidArgument;
            return pending_;
        }
        if (length_ + 1 >= kLineCapacity) {
            pending_ = Status::kLineTooLong;
            return pending_;
        }
        line_[length_++] = character;
        writer_.write(writer_.context, &character, 1);
        return Status::kOk;
    }

private:
    Status Execute()
    {
        const char *arguments[kMaxArguments]{};
        std::size_t count = 0;
        char *cursor = line_;
        while (*cursor != '\0') {
            while (*cursor == ' ' || *cursor == '\t') {
                ++cursor;
            }
            if (*cursor == '\0') {
                break;
            }
            if (count == kMaxArguments) {
                return Status::kTooManyArguments;
            }
            arguments[count++] = cursor;
            while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') {
                ++cursor;
            }
            if (*cursor != '\0') {
                *cursor++ = '\0';
            }
        }
        if (count == 0) {
            return Status::kOk;
        }
        if (std::strcmp(arguments[0], "help") == 0) {
            if (count != 1) {
                return Status::kInvalidArgument;
            }
            writer_.Write("help\n");
            for (std::size_t index = 0; index < count_; ++index) {
                writer_.Write(commands_[index].help);
                writer_.Write("\n");
            }
            return Status::kOk;
        }
        for (std::size_t index = 0; index < count_; ++index) {
            if (std::strcmp(arguments[0], commands_[index].name) == 0) {
                return commands_[index].execute(commands_[index].context,
                                               static_cast<int>(count), arguments, writer_);
            }
        }
        return Status::kUnknownCommand;
    }

    const Command *commands_;
    std::size_t count_;
    Writer writer_;
    char line_[kLineCapacity]{};
    std::size_t length_ = 0;
    Status pending_ = Status::kOk;
    bool after_cr_ = false;
};

}