#include "shell/commands.hpp"

namespace uai::ai::shell {

bool RegisterModels(Engine &engine, Context &context)
{
    return engine.Register({"models", "models [none|person|seg|face|person+face|all|0..7]",
        [](int count, const char *const *args, const Output &out, void *opaque) {
            Request request{Action::kUiStatus, {}};
            if (count == 2) {
                constexpr const char *names[] = {"none", "person", "seg", "face", "person+face", "all"};
                constexpr std::int32_t masks[] = {0, 1, 4, 2, 3, 7};
                request.action = Action::kModels;
                bool valid = ParseInt(args[1], 0, 7, &request.values[0]);
                for (std::size_t index = 0; !valid && index < 6; ++index) {
                    if (std::strcmp(args[1], names[index]) == 0) {
                        request.values[0] = masks[index];
                        valid = true;
                    }
                }
                if (!valid) {
                    out.Write("usage: models [none|person|seg|face|person+face|all|0..7]\r\n");
                    return;
                }
            } else if (count != 1) {
                out.Write("usage: models [none|person|seg|face|person+face|all|0..7]\r\n");
                return;
            }
            static_cast<Context *>(opaque)->Send(request, out);
        }, &context});
}

} // namespace uai::ai::shell