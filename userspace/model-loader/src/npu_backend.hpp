#pragma once
#include "execution.hpp"

extern "C" {
bool experiment_npu_start(std::uint32_t input_bytes, std::uint32_t output_bytes);
int experiment_npu_poll(void);
const std::uint8_t *experiment_npu_output(std::size_t *bytes);
bool experiment_npu_stop(void);
}

namespace experiment::model {

class NpuBackend final : public Backend {
public:
    bool Start(const Manifest &manifest) override
    {
        return experiment_npu_start(manifest.input_bytes, manifest.output_bytes);
    }
    Progress Poll() override
    {
        const auto progress = experiment_npu_poll();
        return progress == 1 ? Progress::kDone : progress == 0 ? Progress::kRunning : Progress::kError;
    }
    const std::uint8_t *Output(std::size_t &bytes) override { return experiment_npu_output(&bytes); }
    bool Stop() override { return experiment_npu_stop(); }
};

}