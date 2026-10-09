#pragma once

#include "operations.hpp"
#include "driver/dma2d_driver/dma2d_driver.hpp"

namespace experiment::graphics {

class Dma2d final {
public:
    bool
    Run(const Request &request,
        std::uint32_t timeout_ms)
    {
        return uai::ai::dma2d::Dma2dManagement::Instance().Run(request, timeout_ms);
    }
};

}