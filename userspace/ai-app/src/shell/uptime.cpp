#include "shell/commands.hpp"

namespace uai::ai::shell {

bool RegisterUptime(
    Engine &engine,
    Context &context
)
{
    return engine.Register(
        {"uptime",
         "uptime",
         [](int count, const char *const *, const Output &out, void *opaque) {
             if (count != 1) {
                 out.Write("usage: uptime\r\n");
                 return;
             }
             out.Printf("uptime %lu ms\r\n", static_cast<unsigned long>(static_cast<Context *>(opaque)->now()));
         },
         &context}
    );
}

} // namespace uai::ai::shell