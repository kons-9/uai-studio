#pragma once
#include "middleware/foundation/error.hpp"
#include <cstddef>

namespace uai::ai::console {
struct Input;
struct Notifier;
namespace registers {
class ConsoleRegisterLayer final {
public:
    common::Error Initialize(Notifier notifier);
    common::Error Read(Input &input);
    common::Error Write(
        const char *text,
        std::size_t size
    );
};
}
}