#pragma once

#include <cstdio>

#include "driver/camera_driver/capture_configuration.hpp"
#include "middleware/foundation/error.hpp"
#include "shell/mailbox.hpp"
#include "ui/ui_layout.hpp"

namespace uai::ai::shell {

struct ExposureMode {
    bool manual = false;
    bool custom_statistics = false;
};

template <typename Ui>
common::Error CheckConstraints(
    const Request &request,
    const Ui &ui,
    const ExposureMode &mode
)
{
    if ((request.action == Action::kCameraAe && request.values[0] == 0 && ui.AiExposureEnabled())
        || (request.action == Action::kCameraStatistics && ui.AiExposureEnabled())
        || (request.action == Action::kCameraManual && (!mode.manual || ui.AiExposureEnabled())))
        return {common::ErrorCode::kInvalidState};
    if (request.action == Action::kAiExposure && request.values[0] != 0 && !ui.AiExposureAvailable())
        return {common::ErrorCode::kInvalidState};
    return {};
}

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
    common::Error status = CheckConstraints(request, ui, mode);
    if (!status.Ok()) {
        reply.code = static_cast<std::int32_t>(status.Code());
        std::snprintf(reply.text, sizeof(reply.text), "rejected: exposure mode conflict\r\n");
        return reply;
    }
    camera::State state{};
    camera::State previous_controls{};
    camera::Geometry geometry{};
    ExposureMode next_mode = mode;
    const auto &values = request.values;
    const bool changes_controls = request.action == Action::kCameraAe || request.action == Action::kCameraManual
        || request.action == Action::kCameraStatistics || request.action == Action::kCameraCompensation;
    if (changes_controls) {
        status = camera.ReadState(&previous_controls);
        if (!status.Ok()) {
            reply.code = static_cast<std::int32_t>(status.Code());
            std::snprintf(reply.text, sizeof(reply.text), "failed: cannot read camera before apply\r\n");
            return reply;
        }
    }
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
        status = camera.AutoExposure(values[0] != 0);
        if (status.Ok()) {
            next_mode.manual = values[0] == 0;
            if (values[0] != 0)
                next_mode.custom_statistics = false;
        }
        break;
    case Action::kCameraCompensation:
        status = camera.Compensation(values[0]);
        break;
    case Action::kCameraManual:
        status = camera.Manual(values[0], values[1]);
        break;
    case Action::kCameraStatistics:
        status = camera.Statistics(
            {static_cast<std::uint32_t>(values[0]),
             static_cast<std::uint32_t>(values[1]),
             static_cast<std::uint32_t>(values[2]),
             static_cast<std::uint32_t>(values[3])}
        );
        if (status.Ok())
            next_mode.custom_statistics = true;
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
            camera::State previous{};
            status = camera.ReadState(&previous);
            if (!status.Ok())
                break;
            status = camera.AutoExposure(true);
            if (!status.Ok())
                break;
            status = camera.ReadState(&state);
            if (status.Ok() && !state.auto_exposure)
                status = {common::ErrorCode::kInvalidState};
            if (!status.Ok()) {
                const auto restored = camera.AutoExposure(previous.auto_exposure);
                std::snprintf(
                    reply.text,
                    sizeof(reply.text),
                    restored.Ok() ? "failed: ae readback; previous ae restored\r\n"
                                  : "failed: ae readback and restore; camera state unknown\r\n"
                );
                break;
            }
            mode = {};
        }
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
            || request.action == Action::kAiExposure || request.action == Action::kUiStatus)) {
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
        if (status.Ok() && request.action == Action::kCameraAe && state.auto_exposure != (values[0] != 0))
            status = {common::ErrorCode::kInvalidState};
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
    if (changes_controls) {
        if (status.Ok()) {
            mode = next_mode;
        } else {
            const auto restored = camera.ApplyState(previous_controls);
            std::snprintf(
                reply.text,
                sizeof(reply.text),
                restored.Ok() ? "failed: camera apply or readback; previous controls restored\r\n"
                              : "failed: camera apply and restore; camera state unknown\r\n"
            );
        }
    }
    if (!status.Ok() && reply.text[0] == '\0')
        std::snprintf(reply.text, sizeof(reply.text), "failed: camera apply or readback; query camera status\r\n");
    return reply;
}

template <
    typename Camera,
    typename Ui>
bool ApplyTouch(
    const ui::Event &event,
    Camera &camera,
    Ui &screen_ui,
    ExposureMode &mode,
    Reply *reply
)
{
    if (event.type != ui::EventType::kTap || reply == nullptr)
        return false;
    Request request{};
    switch (static_cast<app_ui::WidgetId>(event.widget_id)) {
    case app_ui::WidgetId::kPerson:
    case app_ui::WidgetId::kFace:
    case app_ui::WidgetId::kSegmentation: {
        const std::uint8_t bit = event.widget_id == static_cast<std::uint16_t>(app_ui::WidgetId::kPerson) ? 1U
            : event.widget_id == static_cast<std::uint16_t>(app_ui::WidgetId::kFace)                      ? 2U
                                                                                                          : 4U;
        request = {Action::kModels, {static_cast<std::int32_t>(screen_ui.ModelMask() ^ bit)}};
        break;
    }
    case app_ui::WidgetId::kToggleBoxes:
        request = {Action::kBoxes, {screen_ui.ShowBoxes() ? 0 : 1}};
        break;
    case app_ui::WidgetId::kAiExposure:
        request = {Action::kAiExposure, {screen_ui.AiExposureEnabled() ? 0 : 1}};
        break;
    default:
        return false;
    }
    *reply = Apply(request, camera, screen_ui, mode);
    return true;
}

} // namespace uai::ai::shell