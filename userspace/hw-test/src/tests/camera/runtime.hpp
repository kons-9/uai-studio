#pragma once

#include "tests/camera/commands.hpp"
#include "driver/camera_driver/camera_driver.hpp"

namespace uai::hwtest::camera {

using Geometry = uai::ai::camera::Geometry;

inline console::Status Convert(uai::ai::common::Error status)
{
    using Code = uai::ai::common::ErrorCode;
    if (status.Ok())
        return console::Status::kOk;
    if (status.Code() == Code::kInvalidArgument)
        return console::Status::kInvalidArgument;
    if (status.Code() == Code::kNotInitialized || status.Code() == Code::kAlreadyInitialized
        || status.Code() == Code::kInvalidState)
        return console::Status::kInvalidState;
    return console::Status::kHardware;
}

class KernelBackend final : public Backend {
public:
    explicit KernelBackend(uai::ai::camera::CameraManagement &camera) : camera_(camera) {}
    console::Status Read(State &state) override { return Convert(camera_.ReadState(&state)); }
    console::Status AutoExposure(bool enabled) override { return Convert(camera_.AutoExposure(enabled)); }
    console::Status Compensation(int half_stops) override { return Convert(camera_.Compensation(half_stops)); }
    console::Status Manual(
        std::int32_t exposure_us,
        std::int32_t gain_mdB
    ) override
    {
        return Convert(camera_.Manual(exposure_us, gain_mdB));
    }
    console::Status Statistics(Rect rectangle) override { return Convert(camera_.Statistics(rectangle)); }
    console::Status WhiteBalance(std::uint32_t temperature) override
    {
        return Convert(camera_.WhiteBalance(temperature));
    }
    console::Status ListWhiteBalance(const console::Writer &writer) override
    {
        std::uint32_t temperatures[16]{};
        std::size_t count = 0;
        auto status = camera_.ListWhiteBalance(temperatures, 16, &count);
        if (!status.Ok())
            return Convert(status);
        writer.Write("wb: auto");
        for (std::size_t index = 0; index < count; ++index) {
            char text[24];
            std::snprintf(text, sizeof(text), " %lu", static_cast<unsigned long>(temperatures[index]));
            writer.Write(text);
        }
        writer.Write("\n");
        return console::Status::kOk;
    }

private:
    uai::ai::camera::CameraManagement &camera_;
};

class Runtime {
public:
    Runtime(
        uai::ai::camera::CameraManagement &camera,
        const uai::ai::camera::CaptureConfiguration &configuration
    )
        : camera_(camera),
          configuration_(configuration),
          control_(camera),
          geometry_(configuration.geometry)
    {}

    console::Status Start()
    {
        if (Running()) {
            return console::Status::kInvalidState;
        }
        auto status = camera_.Initialize(configuration_);
        if (!status.Ok() && status.Code() != uai::ai::common::ErrorCode::kAlreadyInitialized)
            return Convert(status);
        return Convert(camera_.Start());
    }

    console::Status Stop()
    {
        if (!Running()) {
            return console::Status::kInvalidState;
        }
        Save();
        return Convert(camera_.Stop());
    }

    console::Status Recover(bool capture_settings = true)
    {
        if (Running() && capture_settings) {
            Save();
        }
        return Convert(camera_.Recover());
    }

    console::Status Poll() { return Running() ? Convert(camera_.Process()) : console::Status::kOk; }
    bool Running() const { return camera_.Running(); }
    std::uint32_t Recoveries() const { return camera_.GetDiagnostics().recovery_count; }
    const Geometry &Configuration() const
    {
        (void)camera_.GetGeometry(&geometry_);
        return geometry_;
    }

    console::Status ReadState(State &state)
    {
        return Running() ? control_.Read(state) : console::Status::kInvalidState;
    }

    console::Status RestoreState(const State &state)
    {
        if (!Running()) {
            return console::Status::kInvalidState;
        }
        return Convert(camera_.ApplyState(state));
    }

    console::Status Change(Geometry requested)
    {
        if (!Running()) {
            return console::Status::kInvalidState;
        }
        return Convert(camera_.Configure(requested));
    }

    console::Status Save()
    {
        State current;
        return control_.Read(current);
    }

    static console::Status ControlCommand(
        void *context,
        int count,
        const char *const *arguments,
        const console::Writer &writer
    )
    {
        auto &runtime = *static_cast<Runtime *>(context);
        if (!runtime.Running()) {
            return console::Status::kInvalidState;
        }
        const auto status = Execute(&runtime.control_, count, arguments, writer);
        if (status == console::Status::kOk) {
            return runtime.Save();
        }
        return status;
    }

    static console::Status CaptureCommand(
        void *context,
        int count,
        const char *const *arguments,
        const console::Writer &writer
    )
    {
        auto &runtime = *static_cast<Runtime *>(context);
        auto status = console::Status::kInvalidArgument;
        if (count == 2 && std::strcmp(arguments[1], "start") == 0) {
            status = runtime.Start();
        } else if (count == 2 && std::strcmp(arguments[1], "stop") == 0) {
            status = runtime.Stop();
        } else if (count == 2 && std::strcmp(arguments[1], "recover") == 0) {
            status = runtime.Recover();
        } else if (count >= 3) {
            Geometry request = runtime.Configuration();
            std::int32_t values[4]{};
            if (count > 6) {
                return status;
            }
            for (int index = 2; index < count; ++index) {
                if (!ParseInteger(arguments[index], values[index - 2]) || values[index - 2] < 0) {
                    return status;
                }
            }
            if (count == 3 && std::strcmp(arguments[1], "fps") == 0) {
                request.fps = static_cast<std::uint32_t>(values[0]);
            } else if (count == 4 && std::strcmp(arguments[1], "flip") == 0 && values[0] <= 1 && values[1] <= 1) {
                request.horizontal = values[0] != 0;
                request.vertical = values[1] != 0;
            } else if (count == 6 && std::strcmp(arguments[1], "crop") == 0) {
                request.crop = {
                    static_cast<std::uint32_t>(values[0]),
                    static_cast<std::uint32_t>(values[1]),
                    static_cast<std::uint32_t>(values[2]),
                    static_cast<std::uint32_t>(values[3])
                };
            } else {
                return status;
            }
            status = runtime.Change(request);
        }
        if (status == console::Status::kOk) {
            writer.Write("OK capture\n");
        }
        return status;
    }

private:
    uai::ai::camera::CameraManagement &camera_;
    uai::ai::camera::CaptureConfiguration configuration_;
    KernelBackend control_;
    mutable Geometry geometry_;
};

}
