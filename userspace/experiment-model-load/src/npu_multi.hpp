#pragma once
#include "execution.hpp"

namespace experiment::model {

class MultiBackend final : public Backend {
public:
    bool Start(const Manifest &manifest) override;
    Progress Poll() override;
    const std::uint8_t *Output(std::size_t &bytes) override;
    bool Stop() override;
    std::uint8_t *Input(std::size_t &bytes) override;
    bool AdoptInput(std::size_t bytes) override;
private:
    Manifest active_{};
    bool done_ = false;
};

}