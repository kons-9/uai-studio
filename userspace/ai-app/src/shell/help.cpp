#include "shell/commands.hpp"

namespace uai::ai::shell {

bool RegisterHelp(Engine &engine, Context &context)
{
    return engine.Register({"help", "help [command]", [](int count, const char *const *args, const Output &out, void *opaque) {
        const auto &commands = static_cast<Context *>(opaque)->engine;
        if (count > 2) {
            out.Write("usage: help [command]\r\n");
            return;
        }
        bool found = count == 1;
        for (std::size_t index = 0; index < commands.CommandCount(); ++index) {
            const Command &command = commands.Commands()[index];
            if (count == 1 || std::strcmp(args[1], command.name) == 0) {
                out.Printf("%s\r\n", command.usage);
                found = true;
            }
        }
        if (!found)
            out.Write("error: unknown command\r\n");
    }, &context});
}

} // namespace uai::ai::shell