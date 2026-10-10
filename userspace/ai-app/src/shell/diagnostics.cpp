#include "shell/commands.hpp"

namespace uai::ai::shell {

bool RegisterDiagnostics(Engine &engine, Context &context)
{
    return engine.Register({"diag", "diag <frame|brightness|input|input_display|inference|fps|display|timing> [on|off]",
        [](int count, const char *const *args, const Output &out, void *opaque) {
            constexpr const char *names[] = {"frame", "brightness", "input", "input_display",
                                             "inference", "fps", "display", "timing"};
            Request request{Action::kDiagnostics, {}};
            bool valid = count == 2 || count == 3;
            std::size_t index = 0;
            while (valid && index < 8 && std::strcmp(args[1], names[index]) != 0)
                ++index;
            valid = valid && index != 8;
            request.values[0] = static_cast<std::int32_t>(index);
            request.values[1] = -1;
            if (valid && count == 3)
                valid = ParseOnOff(args[2], &request.values[1]);
            if (!valid) {
                out.Write("usage: diag <frame|brightness|input|input_display|inference|fps|display|timing> [on|off]\r\n");
                return;
            }
            static_cast<Context *>(opaque)->Send(request, out);
        }, &context});
}

} // namespace uai::ai::shell