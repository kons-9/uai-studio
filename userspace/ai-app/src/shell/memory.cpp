#include "shell/commands.hpp"

namespace uai::ai::shell {

bool RegisterMemory(Engine &engine, Context &context)
{
    return engine.Register({"memory", "memory", [](int count, const char *const *, const Output &out, void *opaque) {
        if (count != 1) {
            out.Write("usage: memory\r\n");
            return;
        }
        static_cast<Context *>(opaque)->memory_usage(out);
    }, &context});
}

} // namespace uai::ai::shell