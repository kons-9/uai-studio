#include "shell/commands.hpp"

namespace uai::ai::shell {

bool RegisterTrace(
    Engine &engine,
    Context &context
)
{
    return engine.Register(
        {"trace",
         "trace <ai|cpu> [pause|resume|status]",
         [](int count, const char *const *args, const Output &out, void *opaque) {
             if ((count != 2 && count != 3) || (std::strcmp(args[1], "ai") != 0 && std::strcmp(args[1], "cpu") != 0)) {
                 out.Write("usage: trace <ai|cpu> [pause|resume|status]\r\n");
                 return;
             }
             auto &context = *static_cast<Context *>(opaque);
             const bool cpu = std::strcmp(args[1], "cpu") == 0;
             auto &held = context.trace_held[cpu ? 1U : 0U];
             if (count == 2) {
                 if (context.transfer_trace != nullptr)
                     context.transfer_trace(out, cpu, held);
                 else
                     out.Write("error: trace transfer unavailable\r\n");
                 return;
             }
             if (std::strcmp(args[2], "pause") == 0) {
                 if (!held) {
                     if (context.pause_trace == nullptr) {
                         out.Write("error: trace pause unavailable\r\n");
                         return;
                     }
                     const auto status = context.pause_trace(cpu);
                     if (!status.Ok()) {
                         out.Printf("error: trace pause code=%ld\r\n", static_cast<long>(status.Code()));
                         return;
                     }
                     held = true;
                 }
             } else if (std::strcmp(args[2], "resume") == 0) {
                 if (context.resume_trace == nullptr) {
                     out.Write("error: trace resume unavailable\r\n");
                     return;
                 }
                 context.resume_trace(cpu);
                 held = false;
             } else if (std::strcmp(args[2], "status") != 0) {
                 out.Write("usage: trace <ai|cpu> [pause|resume|status]\r\n");
                 return;
             }
             out.Printf("trace %s=%s\r\n", cpu ? "cpu" : "ai", held ? "paused" : "running");
         },
         &context}
    );
}

} // namespace uai::ai::shell