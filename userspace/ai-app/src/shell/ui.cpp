#include "shell/commands.hpp"

namespace uai::ai::shell {

bool RegisterUi(Engine &engine, Context &context)
{
    return engine.Register({"ui", "ui [status|boxes on|off|exposure on|off]",
        [](int count, const char *const *args, const Output &out, void *opaque) {
            Request request{Action::kUiStatus, {}};
            if (count == 3 && std::strcmp(args[1], "boxes") == 0) {
                request.action = Action::kBoxes;
            } else if (count == 3 && std::strcmp(args[1], "exposure") == 0) {
                request.action = Action::kAiExposure;
            } else if (count != 1 && (count != 2 || std::strcmp(args[1], "status") != 0)) {
                out.Write("usage: ui [status|boxes on|off|exposure on|off]\r\n");
                return;
            }
            if (count == 3 && !ParseOnOff(args[2], &request.values[0])) {
                out.Write("usage: ui [status|boxes on|off|exposure on|off]\r\n");
                return;
            }
            static_cast<Context *>(opaque)->Send(request, out);
        }, &context});
}

} // namespace uai::ai::shell