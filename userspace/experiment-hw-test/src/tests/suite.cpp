#include "suite.hpp"

namespace experiment::hwtest::tests {

const Case cases[] = {
    {"display", display_driver::Run, false, 1000, "LTDC/LCD setup and RGB565 framebuffer readback"},
    {"rng", rng_driver::Run, false, 1000, "64 random words, non-stuck output, seed/clock errors"},
    {"hash", hash_driver::Run, false, 1000, "SHA-256 known answers for 3-byte and 56-byte inputs"},
    {"crc", crc_driver::Run, false, 1000, "CRC-32/MPEG-2 known answer and accumulator reset"},
    {"gpdma", gpdma_driver::Run, false, 2000, "Channel0 SRAM copy at seven byte lengths, guards and cache coherence"},
    {"hpdma", hpdma_driver::Run, false, 2000, "Channel0 SRAM copy at seven byte lengths, guards and cache coherence"},
    {"rtc", rtc_driver::Run, false, 5000, "LSI clock and midnight date/weekday rollover; overwrites test calendar"},
    {"tim", tim_driver::Run, false, 1000, "TIM2 counter rate against DWT cycles and stopped counter"},
    {"sram", sram_driver::Run, false, 1000, "CPU address/data patterns in a dedicated 4 KiB SRAM buffer"},
    {"psram", psram_driver::Run, false, 2000, "XSPI1 patterns after cache clean/invalidate in reserved PSRAM"},
    {"nor-read", nor_driver::Run, false, 12000, "XSPI2 read-only repeat stability; not an expected-content CRC test"},
    {"dma2d", dma2d_driver::Run, false, 1000, "DMA2D ARGB8888 copy, guards and CPU/DMA D-cache coherence"}
};

const std::size_t case_count = sizeof(cases) / sizeof(cases[0]);

}
