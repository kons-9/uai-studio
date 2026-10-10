#include "shell/commands.hpp"

#include "middleware/foundation/log.hpp"

namespace uai::ai::shell {

bool RegisterLog(
    Engine &engine,
    Context &context
)
{
    return engine.Register(
        {"log",
         "log [error|warn|info|debug|trace]",
         [](int count, const char *const *args, const Output &out, void *) {
             constexpr const char *names[] = {"error", "warn", "info", "debug", "trace"};
             if (count == 2) {
                 std::size_t index = 0;
                 while (index < 5 && std::strcmp(args[1], names[index]) != 0)
                     ++index;
                 if (index == 5) {
                     out.Write("usage: log [error|warn|info|debug|trace]\r\n");
                     return;
                 }
                 common::SetLogLevel(static_cast<common::LogLevel>(index));
             } else if (count != 1) {
                 out.Write("usage: log [error|warn|info|debug|trace]\r\n");
                 return;
             }
             out.Printf("log %s\r\n", names[static_cast<std::size_t>(common::GetLogLevel())]);
         },
         &context}
    );
}

} // namespace uai::ai::shell