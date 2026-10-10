#pragma once

#include "exposure_control/controller.hpp"
#include "driver/camera_driver/capture_configuration.hpp"
#include "middleware/foundation/error.hpp"

namespace uai::ai::exposure_control {

class Runtime final {
public:
    void Observe(
        const inference::BoxSet &result,
        std::uint32_t now
    )
    {
        controller_.Observe(result, now);
    }

    const Values &DisplayValues() const { return controller_.DisplayValues(); }

    template <typename Camera>
    common::Error Process(
        Camera &camera,
        std::uint32_t now,
        bool enabled = true,
        bool restore_auto = true,
        bool manage_statistics = true
    )
    {
        if (enabled_ != enabled) {
            enabled_ = enabled;
            have_tick_ = false;
            if (!enabled)
                controller_ = Controller{};
        }
        if (have_tick_ && now - last_tick_ < 250U)
            return {};
        have_tick_ = true;
        last_tick_ = now;
        camera::State state{};
        camera::Geometry geometry{};
        auto status = camera.ReadState(&state);
        if (status.Ok())
            status = camera.GetGeometry(&geometry);
        if (!status.Ok()) {
            controller_.Failed(static_cast<std::uint32_t>(status.Code()));
            return status;
        }
        Mapping mapping{
            state.sensor_width,
            state.sensor_height,
            {geometry.crop.x, geometry.crop.y, geometry.crop.width, geometry.crop.height}
        };
        mapping.horizontal = geometry.horizontal;
        mapping.vertical = geometry.vertical;
        controller_.Step(now, mapping);
        const auto &requested = controller_.DisplayValues().requested;
        if (!enabled && restore_auto && !state.auto_exposure) {
            status = camera.AutoExposure(true);
            if (status.Ok())
                status = camera.ReadState(&state);
        }
        if (manage_statistics && state.auto_exposure
            && (requested.x != state.statistics.x || requested.y != state.statistics.y
                || requested.width != state.statistics.width || requested.height != state.statistics.height)) {
            status = camera.Statistics({requested.x, requested.y, requested.width, requested.height});
            if (status.Ok())
                status = camera.ReadState(&state);
        }
        if (!status.Ok()) {
            controller_.Failed(static_cast<std::uint32_t>(status.Code()));
            return status;
        }
        controller_.Readback(
            {state.statistics.x, state.statistics.y, state.statistics.width, state.statistics.height},
            state.reported_exposure_us,
            state.reported_gain_mdB,
            state.auto_exposure
        );
        return {};
    }

private:
    Controller controller_;
    std::uint32_t last_tick_ = 0;
    bool have_tick_ = false;
    bool enabled_ = true;
};

}