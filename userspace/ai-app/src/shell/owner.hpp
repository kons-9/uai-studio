#pragma once

#include <cstdio>

#include "driver/camera_driver/capture_configuration.hpp"
#include "middleware/foundation/error.hpp"
#include "shell/mailbox.hpp"

namespace uai::ai::shell {

struct ExposureMode {
    bool manual = false;
    bool custom_statistics = false;
};

template <typename Camera>
common::Error FollowUiExposure(
    bool enabled,
    bool previous,
    Camera &camera,
    ExposureMode &mode
)
{
    if (!enabled)
        return {};
    if (!previous)
        mode.custom_statistics = false;
    if (mode.manual) {
        const auto status = camera.AutoExposure(true);
        if (status.Ok())
            mode = {};
        return status;
    }
    return {};
}

template <
    typename Camera,
    typename Ui>
Reply Apply(
    const Request &request,
    Camera &camera,
    Ui &ui,
    ExposureMode &mode
)
{
    Reply reply{};
    common::Error status{};
    camera::State state{};
    camera::Geometry geometry{};
    const auto &values = request.values;
    switch (request.action) {
    case Action::kCameraStatus: {
        status = camera.ReadState(&state);
        if (status.Ok())
            status = camera.GetGeometry(&geometry);
        if (status.Ok()) {
            const auto diagnostics = camera.GetDiagnostics();
            std::snprintf(
                reply.text,
                sizeof(reply.text),
                "camera ae=%u exp=%ldus gain=%ldmdB comp=%d awb=%u temp=%lu stats=%lu,%lu,%lu,%lu\r\n"
                "fps=%lu flip=%u,%u crop=%lu,%lu,%lu,%lu frames=%lu/%lu errors=%lu/%lu/%lu\r\n",
                static_cast<unsigned int>(state.auto_exposure),
                static_cast<long>(state.reported_exposure_us),
                static_cast<long>(state.reported_gain_mdB),
                state.compensation,
                static_cast<unsigned int>(state.auto_white_balance),
                static_cast<unsigned long>(state.color_temperature),
                static_cast<unsigned long>(state.statistics.x),
                static_cast<unsigned long>(state.statistics.y),
                static_cast<unsigned long>(state.statistics.width),
                static_cast<unsigned long>(state.statistics.height),
                static_cast<unsigned long>(geometry.fps),
                static_cast<unsigned int>(geometry.horizontal),
                static_cast<unsigned int>(geometry.vertical),
                static_cast<unsigned long>(geometry.crop.x),
                static_cast<unsigned long>(geometry.crop.y),
                static_cast<unsigned long>(geometry.crop.width),
                static_cast<unsigned long>(geometry.crop.height),
                static_cast<unsigned long>(diagnostics.frame_event_count),
                static_cast<unsigned long>(diagnostics.pipe2_frame_event_count),
                static_cast<unsigned long>(diagnostics.dcmipp_error_count),
                static_cast<unsigned long>(diagnostics.csi_error_count),
                static_cast<unsigned long>(diagnostics.isp_error_count)
            );
        }
        break;
    }
    case Action::kCameraAe:
        if (!values[0] && ui.AiExposureEnabled())
            status = {common::ErrorCode::kInvalidState};
        else
            status = camera.AutoExposure(values[0] != 0);
        if (status.Ok()) {
            mode.manual = values[0] == 0;
            if (values[0] != 0)
                mode.custom_statistics = false;
        }
        break;
    case Action::kCameraCompensation:
        status = camera.Compensation(values[0]);
        break;
    case Action::kCameraManual:
        status = mode.manual ? camera.Manual(values[0], values[1]) : common::Error{common::ErrorCode::kInvalidState};
        break;
    case Action::kCameraStatistics:
        if (ui.AiExposureEnabled())
            status = {common::ErrorCode::kInvalidState};
        else
            status = camera.Statistics(
                {static_cast<std::uint32_t>(values[0]),
                 static_cast<std::uint32_t>(values[1]),
                 static_cast<std::uint32_t>(values[2]),
                 static_cast<std::uint32_t>(values[3])}
            );
        if (status.Ok())
            mode.custom_statistics = true;
        break;
    case Action::kCameraFps:
    case Action::kCameraFlip:
    case Action::kCameraCrop:
        status = camera.GetGeometry(&geometry);
        if (!status.Ok())
            break;
        if (request.action == Action::kCameraFps)
            geometry.fps = static_cast<std::uint32_t>(values[0]);
        else if (request.action == Action::kCameraFlip) {
            geometry.horizontal = values[0] != 0;
            geometry.vertical = values[1] != 0;
        } else {
            geometry.crop = {
                static_cast<std::uint32_t>(values[0]),
                static_cast<std::uint32_t>(values[1]),
                static_cast<std::uint32_t>(values[2]),
                static_cast<std::uint32_t>(values[3])
            };
        }
        status = camera.Configure(geometry);
        break;
    case Action::kModels:
        ui.SetModelMask(static_cast<std::uint8_t>(values[0]));
        break;
    case Action::kBoxes:
        ui.SetShowBoxes(values[0] != 0);
        break;
    case Action::kAiExposure:
        if (values[0] != 0) {
            status = camera.AutoExposure(true);
            if (!status.Ok())
                break;
        }
        mode = {};
        ui.SetAiExposureEnabled(values[0] != 0);
        break;
    case Action::kUiStatus:
        break;
    case Action::kDiagnostics:
        status = {common::ErrorCode::kInvalidArgument};
        break;
    }
    reply.code = static_cast<std::int32_t>(status.Code());
    if (status.Ok()
        && (request.action == Action::kModels || request.action == Action::kBoxes
            || request.action == Action::kUiStatus)) {
        std::snprintf(
            reply.text,
            sizeof(reply.text),
            "models=%u boxes=%u ai_exposure=%u\r\n",
            static_cast<unsigned int>(ui.ModelMask()),
            static_cast<unsigned int>(ui.ShowBoxes()),
            static_cast<unsigned int>(ui.AiExposureEnabled())
        );
    }
    if (status.Ok() && reply.text[0] == '\0') {
        status = camera.ReadState(&state);
        reply.code = static_cast<std::int32_t>(status.Code());
        if (status.Ok())
            std::snprintf(
                reply.text,
                sizeof(reply.text),
                "ok ae=%u exp=%ldus gain=%ldmdB stats=%lu,%lu,%lu,%lu\r\n",
                static_cast<unsigned int>(state.auto_exposure),
                static_cast<long>(state.reported_exposure_us),
                static_cast<long>(state.reported_gain_mdB),
                static_cast<unsigned long>(state.statistics.x),
                static_cast<unsigned long>(state.statistics.y),
                static_cast<unsigned long>(state.statistics.width),
                static_cast<unsigned long>(state.statistics.height)
            );
    }
    return reply;
}

} // namespace uai::ai::shell