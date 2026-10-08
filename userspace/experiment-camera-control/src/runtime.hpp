#pragma once

#include "camera_control.hpp"

namespace experiment::camera {

struct Geometry {
    std::uint32_t fps = 30;
    bool horizontal = false;
    bool vertical = false;
    Rect crop{0, 0, 1620, 1944};
};

class Device {
public:
    virtual ~Device() = default;
    virtual console::Status Open() = 0;
    virtual console::Status Close() = 0;
    virtual console::Status Poll() = 0;
    virtual console::Status Configure(const Geometry &) = 0;
};

class Runtime {
public:
    Runtime(Device &device, Backend &control) : device_(device), control_(control) {}

    console::Status Start()
    {
        if (running_) { return console::Status::kInvalidState; }
        auto status = device_.Open();
        if (status == console::Status::kOk) { status = device_.Configure(geometry_); }
        if (status == console::Status::kOk && saved_) { status = Restore(); }
        if (status != console::Status::kOk) {
            device_.Close();
            return status;
        }
        running_ = true;
        status = Save();
        if (status != console::Status::kOk) { device_.Close(); running_ = false; }
        return status;
    }

    console::Status Stop()
    {
        if (!running_) { return console::Status::kInvalidState; }
        Save();
        const auto status = device_.Close();
        running_ = false;
        return status;
    }

    console::Status Recover(bool capture_settings = true)
    {
        if (running_ && capture_settings) { Save(); }
        running_ = false;
        const auto status = device_.Close();
        if (status != console::Status::kOk) { return status; }
        ++recoveries_;
        return Start();
    }

    console::Status Poll() { return running_ ? device_.Poll() : console::Status::kOk; }
    bool Running() const { return running_; }
    std::uint32_t Recoveries() const { return recoveries_; }
    const Geometry &Configuration() const { return geometry_; }

    console::Status ReadState(State &state)
    {
        return running_ ? control_.Read(state) : console::Status::kInvalidState;
    }

    console::Status RestoreState(const State &state)
    {
        if (!running_) { return console::Status::kInvalidState; }
        const auto status = ApplyState(state);
        return status == console::Status::kOk ? Save() : status;
    }

    console::Status Change(Geometry requested)
    {
        State current;
        if (!running_) { return console::Status::kInvalidState; }
        const auto read = control_.Read(current);
        if (read != console::Status::kOk) { return read; }
        if (requested.fps < 10 || requested.fps > 30 || requested.fps % 5 != 0 || requested.crop.width < 400 || requested.crop.height < 480 ||
            !Inside(requested.crop, current.sensor_width, current.sensor_height)) { return console::Status::kInvalidArgument; }
        const auto status = device_.Configure(requested);
        if (status != console::Status::kOk) {
            Recover(false);
            return status;
        }
        geometry_ = requested;
        return Save();
    }

    console::Status Save()
    {
        State current;
        const auto status = control_.Read(current);
        if (status == console::Status::kOk) { settings_ = current; saved_ = true; }
        return status;
    }

    static console::Status ControlCommand(void *context, int count, const char *const *arguments, const console::Writer &writer)
    {
        auto &runtime = *static_cast<Runtime *>(context);
        if (!runtime.running_) { return console::Status::kInvalidState; }
        const auto status = Execute(&runtime.control_, count, arguments, writer);
        if (status == console::Status::kOk) { return runtime.Save(); }
        return status;
    }

    static console::Status CaptureCommand(void *context, int count, const char *const *arguments, const console::Writer &writer)
    {
        auto &runtime = *static_cast<Runtime *>(context);
        auto status = console::Status::kInvalidArgument;
        if (count == 2 && std::strcmp(arguments[1], "start") == 0) { status = runtime.Start(); }
        else if (count == 2 && std::strcmp(arguments[1], "stop") == 0) { status = runtime.Stop(); }
        else if (count == 2 && std::strcmp(arguments[1], "recover") == 0) { status = runtime.Recover(); }
        else if (count >= 3) {
            Geometry request = runtime.geometry_;
            std::int32_t values[4]{};
            if (count > 6) { return status; }
            for (int index = 2; index < count; ++index) {
                if (!ParseInteger(arguments[index], values[index - 2]) || values[index - 2] < 0) { return status; }
            }
            if (count == 3 && std::strcmp(arguments[1], "fps") == 0) { request.fps = static_cast<std::uint32_t>(values[0]); }
            else if (count == 4 && std::strcmp(arguments[1], "flip") == 0 && values[0] <= 1 && values[1] <= 1) {
                request.horizontal = values[0] != 0; request.vertical = values[1] != 0;
            } else if (count == 6 && std::strcmp(arguments[1], "crop") == 0) {
                request.crop = {static_cast<std::uint32_t>(values[0]), static_cast<std::uint32_t>(values[1]),
                                static_cast<std::uint32_t>(values[2]), static_cast<std::uint32_t>(values[3])};
            } else { return status; }
            status = runtime.Change(request);
        }
        if (status == console::Status::kOk) { writer.Write("OK capture\n"); }
        return status;
    }

private:
    console::Status Restore()
    {
        return ApplyState(settings_);
    }

    console::Status ApplyState(const State &state)
    {
        auto status = control_.AutoExposure(false);
        if (status == console::Status::kOk) { status = control_.Compensation(state.compensation); }
        if (status == console::Status::kOk) { status = control_.Statistics(state.statistics); }
        if (status == console::Status::kOk) { status = control_.WhiteBalance(state.auto_white_balance ? 0 : state.color_temperature); }
        if (status == console::Status::kOk && state.reported_exposure_us > 0) {
            status = control_.Manual(state.reported_exposure_us, state.reported_gain_mdB);
        }
        if (status == console::Status::kOk) { status = control_.AutoExposure(state.auto_exposure); }
        return status;
    }

    Device &device_;
    Backend &control_;
    State settings_{};
    Geometry geometry_{};
    bool saved_ = false;
    bool running_ = false;
    std::uint32_t recoveries_ = 0;
};

}
