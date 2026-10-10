#include "shell/commands.hpp"

namespace uai::ai::shell {

bool RegisterCamera(
    Engine &engine,
    Context &context
)
{
    return engine.Register(
        {"camera",
         "camera [status|ae on|off|comp -4..4|manual us mdB|stats x y w h|fps 10..30|flip h v|crop x y w h]",
         [](int count, const char *const *args, const Output &out, void *opaque) {
             Request request{};
             bool valid = true;
             if (count == 1 || (count == 2 && std::strcmp(args[1], "status") == 0)) {
                 request.action = Action::kCameraStatus;
             } else if (count == 3 && std::strcmp(args[1], "ae") == 0) {
                 request.action = Action::kCameraAe;
                 valid = ParseOnOff(args[2], &request.values[0]);
             } else if (count == 3 && std::strcmp(args[1], "comp") == 0) {
                 request.action = Action::kCameraCompensation;
                 valid = ParseInt(args[2], -4, 4, &request.values[0]);
             } else if (count == 4 && std::strcmp(args[1], "manual") == 0) {
                 request.action = Action::kCameraManual;
                 valid = ParseInt(args[2], 1, 1000000, &request.values[0])
                     && ParseInt(args[3], 0, 120000, &request.values[1]);
             } else if (count == 3 && std::strcmp(args[1], "fps") == 0) {
                 request.action = Action::kCameraFps;
                 valid = ParseInt(args[2], 10, 30, &request.values[0]) && request.values[0] % 5 == 0;
             } else if (count == 4 && std::strcmp(args[1], "flip") == 0) {
                 request.action = Action::kCameraFlip;
                 valid = ParseOnOff(args[2], &request.values[0]) && ParseOnOff(args[3], &request.values[1]);
             } else if (count == 6 && (std::strcmp(args[1], "crop") == 0 || std::strcmp(args[1], "stats") == 0)) {
                 request.action = std::strcmp(args[1], "crop") == 0 ? Action::kCameraCrop : Action::kCameraStatistics;
                 valid = ParseInt(args[2], 0, 2592, &request.values[0])
                     && ParseInt(args[3], 0, 1944, &request.values[1]) && ParseInt(args[4], 1, 2592, &request.values[2])
                     && ParseInt(args[5], 1, 1944, &request.values[3]);
             } else {
                 valid = false;
             }
             if (!valid) {
                 out.Write(
                     "usage: camera [status|ae on|off|comp -4..4|manual us mdB|stats x y w h|fps 10..30|flip h v|crop "
                     "x y w h]\r\n"
                 );
                 return;
             }
             static_cast<Context *>(opaque)->Send(request, out);
         },
         &context}
    );
}

} // namespace uai::ai::shell