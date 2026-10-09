#include "suite.hpp"
#include "integration.hpp"

namespace experiment::hwtest::tests {

const Case cases[] = {
    {"display", display_driver::Run, false, 1000, "LTDC/LCD setup and RGB565 framebuffer readback"},
    {"rng", rng_driver::Run, false, 1000, "64 random words, non-stuck output, seed/clock errors"},
    {"hash", hash_driver::Run, false, 1000, "SHA-256 known answers for 3-byte and 56-byte inputs"},
    {"crc", crc_driver::Run, false, 1000, "CRC-32/MPEG-2 known answer and accumulator reset"},
    {"gpdma", gpdma_driver::Run, false, 2000, "Channel0 SRAM copy at seven byte lengths, guards and cache coherence"},
    {"hpdma", hpdma_driver::Run, false, 2000, "Channel0 SRAM copy at seven byte lengths, guards and cache coherence"},
    {"rtc", rtc_driver::Run, false, 5000, "LSI clock and midnight date/weekday rollover; overwrites test calendar"},
    {"tim", tim_driver::Run, false, 2000, "TIM2 counter rate against DWT cycles and stopped counter"},
    {"sram", sram_driver::Run, false, 1000, "CPU address/data patterns in a dedicated 4 KiB SRAM buffer"},
    {"psram", psram_driver::Run, false, 2000, "XSPI1 patterns after cache clean/invalidate in reserved PSRAM"},
    {"nor-read", nor_driver::Run, false, 12000, "XSPI2 read-only repeat stability; not an expected-content CRC test"},
    {"dma2d", dma2d_driver::Run, false, 1000, "DMA2D driver RGB565 copy, guards and CPU/DMA D-cache coherence"},
    {"camera-pipes",
     integrated::CameraPipes,
     false,
     65000,
     "Both pipes progress for 60s without stalls, errors or recovery; visual review required",
     false,
     true},
    {"camera-control",
     integrated::CameraControl,
     false,
     120000,
     "32 ISP/geometry/stop/restart/recovery stages with state restoration; visual review required",
     false,
     true},
    {"dma2d-suite",
     integrated::Dma2dSuite,
     false,
     90000,
     "25 pixel/guard/cache cases and 60s camera/LCD concurrent stress; GPU2D excluded",
     false,
     true},
    {"touch-read", integrated::TouchRead, false, 1000, "GT911 ID, initialization and repeated live input reads"},
    {"touch",
     integrated::TouchInteractive,
     false,
     65000,
     "Press and release five displayed targets; explicit interactive test",
     true}
};

const std::size_t case_count = sizeof(cases) / sizeof(cases[0]);

}
