#include "dma2d.hpp"
#include <cstdlib>
#include <iostream>
void Require(bool condition) { if (!condition) { std::cerr << calls << '\n'; std::exit(1); } }
int main()
{
    experiment::graphics::Dma2d device;
    experiment::graphics::Image destination{reinterpret_cast<std::uint8_t *>(0x34000000), 32, 8, 2, 16, experiment::graphics::Format::kRgb565};
    Require(device.Run({experiment::graphics::Operation::kFill, {}, {}, destination, 0xff0000}, 100));
    Require(fill_color == 0xff0000 && configured.Init.ColorMode == DMA2D_OUTPUT_RGB565);
    Require(calls == "clock init prepare barrier start poll barrier invalidate ");
    experiment::graphics::Image source{reinterpret_cast<std::uint8_t *>(0x34001000), 64, 8, 2, 24, experiment::graphics::Format::kRgb888};
    calls.clear(); poll_status = HAL_TIMEOUT; abort_status = HAL_TIMEOUT;
    Require(!device.Run({experiment::graphics::Operation::kBlit, source, {}, destination}, 100));
    Require(configured.LayerCfg[1].RedBlueSwap == DMA2D_RB_SWAP);
    Require(calls == "clock init layer clean prepare barrier start poll abort reset barrier release barrier invalidate ");
    poll_status = HAL_OK; abort_status = HAL_OK; calls.clear();
    Require(device.Run({experiment::graphics::Operation::kBlend, source, source, destination, 0, 128}, 100));
    Require(configured.LayerCfg[1].InputAlpha == 128 && configured.LayerCfg[0].InputAlpha == 255);
    calls.clear(); destination.data += 1;
    Require(!device.Run({experiment::graphics::Operation::kFill, {}, {}, destination}, 100) && calls.empty());
}