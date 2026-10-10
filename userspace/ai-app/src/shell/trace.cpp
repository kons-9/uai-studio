#include "shell/commands.hpp"

namespace uai::ai::shell {

bool RegisterTrace(
    Engine &engine,
    Context &context
)
{
    return engine.Register(
        {"trace",
         "trace <ai|cpu>",
         [](int count, const char *const *args, const Output &out, void *opaque) {
             if (count != 2 || (std::strcmp(args[1], "ai") != 0 && std::strcmp(args[1], "cpu") != 0)) {
                 out.Write("usage: trace <ai|cpu>\r\n");
                 return;
             }
             static_cast<Context *>(opaque)->transfer_trace(out, std::strcmp(args[1], "cpu") == 0);
         },
         &context}
    );
}

} // namespace uai::ai::shell