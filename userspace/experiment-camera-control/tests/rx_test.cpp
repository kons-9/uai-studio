#include "rx_queue.hpp"
#include <cstdlib>

int main()
{
    experiment::console::RxQueue queue;
    for (unsigned iteration = 0; iteration < 8; ++iteration) {
        for (unsigned index = 0; index < 256; ++index) {
            if (!queue.Push(static_cast<char>(index))) { return 1; }
        }
        if (queue.Push('x') || !queue.TakeError() || queue.TakeError()) { return 2; }
        for (unsigned index = 0; index < 256; ++index) {
            char value = 0;
            if (!queue.Pop(value) || static_cast<unsigned char>(value) != index) { return 3; }
        }
        if (!queue.Empty()) { return 4; }
    }
    char value = 0;
    if (queue.Pop(value)) { return 5; }
}