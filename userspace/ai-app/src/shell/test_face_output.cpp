#include "shell/commands.hpp"

namespace uai::ai::shell {

bool RegisterTestFaceOutput(
    Engine &engine,
    Context &context
)
{
    return engine.Register(
        {"test-face",
         "test-face [on [1..16]|off]",
         [](int count, const char *const *args, const Output &out, void *opaque) {
             Request request{Action::kTestFaceOutput, {}};
             bool valid = false;
             if (count == 2 && std::strcmp(args[1], "off") == 0) {
                 request.values[0] = 0;
                 valid = true;
             } else if (count == 2 && std::strcmp(args[1], "on") == 0) {
                 request.values[0] = 1;
                 request.values[1] = 2;
                 valid = true;
             } else if (count == 3 && std::strcmp(args[1], "on") == 0) {
                 request.values[0] = 1;
                 valid = ParseInt(args[2], 1, 16, &request.values[1]);
             }
             if (!valid) {
                 out.Write("usage: test-face [on [1..16]|off]\r\n");
                 return;
             }
             static_cast<Context *>(opaque)->Send(request, out);
         },
         &context}
    );
}

} // namespace uai::ai::shell
