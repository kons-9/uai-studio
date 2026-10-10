#pragma once

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace uai::ai::shell {

struct Output {
    void *context = nullptr;
    void (*write)(void *, const char *, std::size_t) = nullptr;

    void Write(const char *text) const
    {
        if (write != nullptr)
            write(context, text, std::strlen(text));
    }

    template <typename... Args>
    void Printf(const char *format, Args... args) const
    {
        char buffer[256]{};
        const int count = std::snprintf(buffer, sizeof(buffer), format, args...);
        if (count > 0 && write != nullptr)
            write(context, buffer, static_cast<std::size_t>(count) < sizeof(buffer)
                    ? static_cast<std::size_t>(count) : sizeof(buffer) - 1U);
    }
};

using Handler = void (*)(int, const char *const *, const Output &, void *);

struct Command {
    const char *name;
    const char *usage;
    Handler run;
    void *context;
};

class Engine final {
public:
    static constexpr std::size_t kLineCapacity = 96;
    static constexpr std::size_t kMaxArguments = 8;
    static constexpr std::size_t kMaxCommands = 32;

    explicit Engine(Output output, bool echo = false) : output_(output), echo_(echo) {}

    bool Register(Command command)
    {
        if (command.name == nullptr || command.usage == nullptr || command.run == nullptr
            || count_ == kMaxCommands || std::strchr(command.name, ' ') != nullptr)
            return false;
        for (std::size_t index = 0; index < count_; ++index) {
            if (std::strcmp(commands_[index].name, command.name) == 0)
                return false;
        }
        commands_[count_++] = command;
        return true;
    }

    const Command *Commands() const { return commands_; }
    std::size_t CommandCount() const { return count_; }

    void Feed(char value, bool error = false)
    {
        if (error) {
            length_ = 0;
            discard_ = true;
            if (echo_)
                output_.Write("\r\n");
            output_.Write("error: uart receive\r\n");
            return;
        }
        if (value == '\r' || value == '\n') {
            if (discard_) {
                discard_ = false;
                length_ = 0;
            } else if (length_ != 0) {
                if (echo_)
                    output_.Write("\r\n");
                line_[length_] = '\0';
                Execute();
                length_ = 0;
            }
            return;
        }
        if (discard_)
            return;
        if (value == '\b' || value == 0x7f) {
            if (length_ != 0) {
                --length_;
                if (echo_)
                    output_.Write("\b \b");
            }
            return;
        }
        if (value < ' ' || value > '~')
            return;
        if (length_ + 1 >= kLineCapacity) {
            discard_ = true;
            length_ = 0;
            if (echo_)
                output_.Write("\r\n");
            output_.Write("error: line too long\r\n");
            return;
        }
        line_[length_++] = value;
        if (echo_ && output_.write != nullptr)
            output_.write(output_.context, &value, 1U);
    }

private:
    void Execute()
    {
        const char *arguments[kMaxArguments]{};
        std::size_t argument_count = 0;
        char *cursor = line_;
        while (*cursor != '\0') {
            while (*cursor == ' ' || *cursor == '\t')
                ++cursor;
            if (*cursor == '\0')
                break;
            if (argument_count == kMaxArguments) {
                output_.Write("error: too many arguments\r\n");
                return;
            }
            arguments[argument_count++] = cursor;
            while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t')
                ++cursor;
            if (*cursor != '\0')
                *cursor++ = '\0';
        }
        if (argument_count == 0)
            return;
        for (std::size_t index = 0; index < count_; ++index) {
            if (std::strcmp(commands_[index].name, arguments[0]) == 0) {
                commands_[index].run(static_cast<int>(argument_count), arguments, output_, commands_[index].context);
                return;
            }
        }
        output_.Write("error: unknown command\r\n");
    }

    Output output_;
    Command commands_[kMaxCommands]{};
    std::size_t count_ = 0;
    char line_[kLineCapacity]{};
    std::size_t length_ = 0;
    bool discard_ = false;
    bool echo_ = false;
};

} // namespace uai::ai::shell