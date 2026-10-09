#include "rx_queue.hpp"
#include "shell.hpp"
#include <cstdlib>
#include <iostream>
#include <string>

void Require(bool condition)
{
    if (!condition) {
        std::cerr << "rx check failed\n";
        std::exit(1);
    }
}

void TestQueue()
{
    experiment::console::RxQueue queue;
    for (unsigned iteration = 0; iteration < 8; ++iteration) {
        for (unsigned index = 0; index < 256; ++index) {
            Require(queue.Push(static_cast<char>(index)));
        }
        Require(!queue.Push('x'));
        for (unsigned index = 0; index < 256; ++index) {
            char value = 0;
            bool error = false;
            Require(queue.Pop(value, error) && static_cast<unsigned char>(value) == index);
            Require(error == (iteration != 0 && index == 0));
        }
        Require(queue.Empty());
    }
    char value = 0;
    bool error = false;
    Require(!queue.Pop(value, error));
    Require(queue.Push('\r') && queue.Pop(value, error) && value == '\r' && error);
    Require(queue.Push('x') && queue.Pop(value, error) && value == 'x' && !error);
}

experiment::console::Status Execute(
    void *context,
    int,
    const char *const *,
    const experiment::console::Writer &
)
{
    ++*static_cast<unsigned *>(context);
    return experiment::console::Status::kOk;
}

void Write(
    void *context,
    const char *text,
    std::size_t size
)
{
    static_cast<std::string *>(context)->append(text, size);
}

void Push(
    experiment::console::RxQueue &queue,
    const char *text
)
{
    for (; *text; ++text) {
        Require(queue.Push(*text));
    }
}

void Drain(
    experiment::console::RxQueue &queue,
    experiment::console::Shell &shell
)
{
    char character = 0;
    bool error = false;
    while (queue.Pop(character, error)) {
        shell.Feed(character, error);
    }
}

void TestErrorLines()
{
    unsigned valid = 0, damaged = 0, recovered = 0;
    const experiment::console::Command commands[] = {
        {"valid", "valid", Execute, &valid},
        {"damaged", "damaged", Execute, &damaged},
        {"recovered", "recovered", Execute, &recovered}
    };
    std::string output;
    experiment::console::Shell shell(commands, 3, {&output, Write});
    experiment::console::RxQueue queue;
    Push(queue, "valid\rdam");
    queue.Error();
    Push(queue, "aged\r");
    queue.Error();
    Push(queue, "damaged\nrecovered\r\n");
    Drain(queue, shell);
    Require(valid == 1 && damaged == 0 && recovered == 1);
    const auto first_error = output.find("ERR uart-receive");
    const auto second_error = output.find("ERR uart-receive", first_error + 1);
    Require(
        first_error != std::string::npos && second_error != std::string::npos
        && output.find("ERR uart-receive", second_error + 1) == std::string::npos
    );
    Push(queue, "dam");
    Drain(queue, shell);
    queue.Error();
    Push(queue, "aged\x03recovered\rrecovered\n");
    Drain(queue, shell);
    Require(damaged == 0 && recovered == 2);
    Push(queue, "damaged");
    queue.Error();
    Push(queue, "\r\nrecovered\r");
    Drain(queue, shell);
    Require(damaged == 0 && recovered == 3);
}

void TestOverflowLines()
{
    unsigned valid = 0, damaged = 0, recovered = 0;
    const experiment::console::Command commands[] = {
        {"valid", "valid", Execute, &valid},
        {"damaged", "damaged", Execute, &damaged},
        {"recovered", "recovered", Execute, &recovered}
    };
    std::string output;
    experiment::console::Shell shell(commands, 3, {&output, Write});
    experiment::console::RxQueue queue;
    for (unsigned index = 0; index < 41; ++index) {
        Push(queue, "valid\r");
    }
    Push(queue, "damaged   ");
    Require(!queue.Push('x'));
    Drain(queue, shell);
    Push(queue, "\rrecovered\r");
    Drain(queue, shell);
    Require(valid == 41 && damaged == 0 && recovered == 1);
    for (unsigned index = 0; index < 41; ++index) {
        Push(queue, "valid\r");
    }
    Push(queue, "damaged   ");
    Require(!queue.Push('\r'));
    Drain(queue, shell);
    Push(queue, "recovered\rrecovered\r");
    Drain(queue, shell);
    Require(valid == 82 && damaged == 0 && recovered == 2);
}

int main()
{
    TestQueue();
    TestErrorLines();
    TestOverflowLines();
}