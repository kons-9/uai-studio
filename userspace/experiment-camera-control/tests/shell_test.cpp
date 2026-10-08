#include "shell.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void Require(bool condition)
{
    if (!condition) {
        std::cerr << "shell check failed\n";
        std::exit(1);
    }
}

struct Recorder {
    std::string output;
    std::vector<std::string> arguments;
    int calls = 0;
};

experiment::console::Status Record(void *context, int count, const char *const *arguments,
                                   const experiment::console::Writer &writer)
{
    auto &recorder = *static_cast<Recorder *>(context);
    recorder.arguments.assign(arguments, arguments + count);
    ++recorder.calls;
    writer.Write("executed\n");
    return experiment::console::Status::kOk;
}

experiment::console::Status Send(experiment::console::Shell &shell, const std::string &text)
{
    auto status = experiment::console::Status::kOk;
    for (char character : text) {
        status = shell.Feed(character);
    }
    return status;
}

}

int main()
{
    Recorder recorder;
    const experiment::console::Command commands[] = {{"run", "run <value>", Record, &recorder}};
    experiment::console::Shell shell(commands, 1, {&recorder, [](void *context, const char *text, std::size_t size) {
        static_cast<Recorder *>(context)->output.append(text, size);
    }});
    Require(Send(shell, "  run\t12x\b3\r\n") == experiment::console::Status::kOk);
    Require(recorder.calls == 1 && recorder.arguments == std::vector<std::string>({"run", "123"}));
    Require(recorder.output.find("executed\n> ") != std::string::npos);
    Require(Send(shell, "missing\n") == experiment::console::Status::kUnknownCommand);
    Require(Send(shell, "run 1 2 3 4 5 6 7 8\n") == experiment::console::Status::kTooManyArguments);
    Require(Send(shell, "run " + std::string(128, 'x') + "\brun\n") == experiment::console::Status::kLineTooLong);
    Require(recorder.calls == 1);
    Require(Send(shell, "run\x1b[A\n") == experiment::console::Status::kInvalidArgument);
    Require(recorder.calls == 1);
    Require(Send(shell, "run\x03\nhelp\n") == experiment::console::Status::kOk);
    Require(recorder.calls == 1 && recorder.output.find("help\nrun <value>\n") != std::string::npos);
    Require(Send(shell, "run 1 2 3 4 5 6 7\n") == experiment::console::Status::kOk);
    Require(recorder.calls == 2 && recorder.arguments.size() == 8);
    Require(Send(shell, "run " + std::string(123, 'x') + "\n") == experiment::console::Status::kOk);
    Require(recorder.calls == 3);
}