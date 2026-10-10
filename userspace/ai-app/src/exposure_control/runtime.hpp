#pragma once

#include "exposure_control/controller.hpp"
#include "driver/camera_driver/capture_configuration.hpp"
#include "middleware/foundation/error.hpp"
#include "middleware/task/periodic.hpp"

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
    void Reset()
    {
        controller_ = Controller{};
        period_.Reset();
    }
    std::uint32_t RemainingWait(
        std::uint32_t now,
        bool enabled = true
    ) const
    {
        return enabled_ != enabled ? 0U : period_.RemainingWait(now);
    }

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
            period_.Reset();
            if (!enabled)
                controller_ = Controller{};
        }
        if (period_.Period() == 0U)
            period_.Configure(250U, now);
        if (!period_.Take(now))
            return {};
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
    common::TimePeriod period_;
    bool enabled_ = true;
};

}