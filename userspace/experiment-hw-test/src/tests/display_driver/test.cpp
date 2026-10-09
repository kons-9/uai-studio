#include "display_log.hpp"
#include "tests/board.hpp"

#include <cstdio>

namespace experiment::hwtest::tests::display_driver {

Result Run(const Context &context)
{
    if (!display_log::SelfTest()) {
        return {Outcome::kFail, "display-initialization-or-framebuffer"};
    }

    constexpr std::uint32_t kHeight = 480U;
    const auto gcr = LTDC->GCR;
    const auto layer_cr = LTDC_Layer1->CR;
    const auto pixel_format = LTDC_Layer1->PFCR;
    const auto framebuffer = LTDC_Layer1->CFBAR;
    const auto line_count = LTDC_Layer1->CFBLNR;
    char line[192];
    std::snprintf(
        line,
        sizeof(line),
        "TRACE display regs gcr=%08lx l1cr=%08lx pfcr=%08lx cfbar=%08lx cfblnr=%08lx",
        static_cast<unsigned long>(gcr),
        static_cast<unsigned long>(layer_cr),
        static_cast<unsigned long>(pixel_format),
        static_cast<unsigned long>(framebuffer),
        static_cast<unsigned long>(line_count)
    );
    context.Trace(line);

    const auto expected_framebuffer = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(experiment_hwtest_display_framebuffer)
    );
    if ((gcr & LTDC_GCR_LTDCEN) == 0U || (layer_cr & LTDC_LxCR_LEN) == 0U
        || pixel_format != LTDC_PIXEL_FORMAT_RGB565 || framebuffer != expected_framebuffer
        || (line_count & LTDC_LxCFBLNR_CFBLNBR) != kHeight) {
        std::snprintf(
            line,
            sizeof(line),
            "ltdc-reg gcr=%08lx l1cr=%08lx pfcr=%lu cfbar=%08lx exp=%08lx lines=%lu exp=%lu",
            static_cast<unsigned long>(gcr),
            static_cast<unsigned long>(layer_cr),
            static_cast<unsigned long>(pixel_format),
            static_cast<unsigned long>(framebuffer),
            static_cast<unsigned long>(expected_framebuffer),
            static_cast<unsigned long>(line_count & LTDC_LxCFBLNR_CFBLNBR),
            static_cast<unsigned long>(kHeight)
        );
        return {Outcome::kFail, line};
    }
    return {Outcome::kPass, "ltdc-enabled-rgb565-framebuffer-registers"};
}

}
