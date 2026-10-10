#include "shell/commands.hpp"

namespace uai::ai::shell {

bool RegisterTasks(Engine &engine, Context &context)
{
    return engine.Register({"tasks", "tasks", [](int count, const char *const *, const Output &out, void *opaque) {
        if (count != 1) {
            out.Write("usage: tasks\r\n");
            return;
        }
        static_cast<Context *>(opaque)->list_tasks(out);
    }, &context});
}

} // namespace uai::ai::shell