#include "task/camera_render/camera_render_context_and_operations.hpp"

#include "driver/cache_driver/cache_driver.hpp"
#include "driver/camera_driver/camera_driver.hpp"
#include "driver/camera_driver/camera_diagnostics.hpp"
#include "driver/lcd_driver/lcd_driver.hpp"
#include "memory_manager/memory_manager.hpp"
#include "memory_manager/memory_sizes.hpp"
#include "middleware/foundation/log.hpp"
#include "middleware/pipeline/image_diagnostics.hpp"
#include "middleware/pipeline/image_format.hpp"
#include "middleware/task/task.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
}

namespace uai::ai::task::camera_render {

namespace {

void LogFrozenCaptureRegistersAndPixelRows(
    const CameraRenderContext &context,
    const pipeline::CaptureFrame &frame
)
{
    if (!common::IsLogEnabled(common::LogLevel::kDebug)) {
        return;
    }
    const auto camera_diagnostics = context.camera.GetDiagnostics();
    const auto *pixels = reinterpret_cast<const std::uint16_t *>(frame.buffer.address);
    const std::uint32_t full_crc =
        pipeline::Crc32Bytes(reinterpret_cast<const std::uint8_t *>(frame.buffer.address), frame.buffer.size);
    camera::DumpCaptureRegisters(frame, camera_diagnostics, full_crc);

    const auto rows = pipeline::InspectCaptureRows(pixels);
    UAI_LOG_DEBUG(
        "camera: frozen row distribution nonzero=%u zero=%u first=%u last=%u distinct_crc=%u\n",
        rows.nonzero_rows,
        rows.zero_rows,
        rows.nonzero_rows == 0U ? 0U : rows.first_nonzero,
        rows.nonzero_rows == 0U ? 0U : rows.last_nonzero,
        rows.distinct_row_crcs
    );
    std::uint32_t reported_nonzero = 0U;
    for (std::uint32_t y = 0U; y < pipeline::kCaptureFormat.height && reported_nonzero < 24U; ++y) {
        if (rows.has_data[y]) {
            UAI_LOG_DEBUG("camera: frozen nonzero row y=%u crc=%x\n", y, rows.row_crcs[y]);
            ++reported_nonzero;
        }
    }

    constexpr std::uint32_t kRows[] = {0U, 1U, 35U, 194U, 240U, 400U, 479U};
    constexpr std::uint32_t kColumns[] = {0U, 1U, 16U, 39U, 40U, 799U};
    for (const std::uint32_t y : kRows) {
        UAI_LOG_DEBUG("camera: frozen row y=%u crc=%x samples=", y, rows.row_crcs[y]);
        for (const std::uint32_t x : kColumns) {
            UAI_LOG_DEBUG(
                "%04x%s",
                static_cast<unsigned int>(pixels[y * pipeline::kCaptureFormat.width + x]),
                x == kColumns[sizeof(kColumns) / sizeof(kColumns[0]) - 1U] ? "\n" : ","
            );
        }
    }
}

}

void ShowInitialDisplayAndStartCamera(
    const CameraRenderContext &context,
    const inference::BoxSet &initial
)
{
    UAI_LOG_TEXT(uai::ai::common::LogLevel::kDebug, "lcd: driver ready\n");
    UAI_LOG_TEXT(uai::ai::common::LogLevel::kDebug, "lcd: initial frame begin\n");
    auto status = context.lcd.ShowInitialFrame(initial, kDisplayCoordinatePatternDiagnostic);
    if (!status.Ok()) {
        status.LogStatus("lcd");
        common::Task::Halt("ai: initial frame failed\n");
    }
    UAI_LOG_TEXT(uai::ai::common::LogLevel::kDebug, "lcd: initial buffer ready box_count=0\n");

    if constexpr (kDisplayCoordinatePatternDiagnostic) {
        status = context.lcd.SynchronizeCurrentFrame();
        if (!status.Ok()) {
            status.LogStatus("lcd");
            common::Task::Halt("ai: diagnostic frame did not latch\n");
        }
        UAI_LOG_DEBUG(
            "lcd: diagnostic readback fb=%x cr=%x pfcr=%x cfblr=%x cfblnr=%x awcr=%x twcr=%x isr=%x\n",
            static_cast<unsigned int>(LTDC_Layer1->CFBAR),
            static_cast<unsigned int>(LTDC_Layer1->CR),
            static_cast<unsigned int>(LTDC_Layer1->PFCR),
            static_cast<unsigned int>(LTDC_Layer1->CFBLR),
            static_cast<unsigned int>(LTDC_Layer1->CFBLNR),
            static_cast<unsigned int>(LTDC->AWCR),
            static_cast<unsigned int>(LTDC->TWCR),
            static_cast<unsigned int>(LTDC->ISR)
        );
        UAI_LOG_TEXT(
            uai::ai::common::LogLevel::kDebug, "lcd: static coordinate pattern active; camera remains stopped\n"
        );
        for (;;) {
            tk_dly_tsk(1000);
        }
    }

    if constexpr (kSyntheticComposeDiagnostic) {
        status = context.lcd.SynchronizeCurrentFrame();
        if (!status.Ok()) {
            status.LogStatus("lcd");
            common::Task::Halt("ai: initial diagnostic frame did not latch\n");
        }

        std::uintptr_t capture0 = 0U;
        std::uintptr_t capture1 = 0U;
        status = context.memory.CaptureBuffers(&capture0, &capture1);
        if (!status.Ok()) {
            status.LogStatus("memory");
            common::Task::Halt("ai: diagnostic capture buffers unavailable\n");
        }
        const uai::ai::buffer::Buffer source_buffer{
            capture0, uai::ai::memory_manager::kCaptureBufferBytes, 0U, uai::ai::buffer::Region::kCapture
        };
        status = context.cache.PrepareForDmaWrite(source_buffer);
        if (!status.Ok()) {
            status.LogStatus("memory");
            common::Task::Halt("ai: diagnostic source cache prepare failed\n");
        }
        status = context.lcd.GenerateCoordinatePattern(source_buffer);
        if (!status.Ok()) {
            status.LogStatus("lcd");
            common::Task::Halt("ai: diagnostic pattern generation failed\n");
        }
        SCB_CleanDCache_by_Addr(
            reinterpret_cast<std::uint32_t *>(source_buffer.address), static_cast<std::int32_t>(source_buffer.size)
        );

        uai::ai::pipeline::CaptureFrame synthetic_capture{};
        status = context.memory.ImportCompletedCapture(source_buffer.address, &synthetic_capture);
        if (!status.Ok()) {
            status.LogStatus("memory");
            common::Task::Halt("ai: diagnostic capture import failed\n");
        }
        UAI_LOG_DEBUG(
            "lcd: synthetic compose begin sequence=%u source=%x bytes=%u\n",
            static_cast<unsigned int>(synthetic_capture.sequence),
            static_cast<unsigned int>(source_buffer.address),
            static_cast<unsigned int>(source_buffer.size)
        );
        status = context.lcd.ComposeAndPresent(synthetic_capture, initial, nullptr, true);
        if (!status.Ok()) {
            status.LogStatus("lcd");
            common::Task::Halt("ai: synthetic compose failed\n");
        }
        UAI_LOG_TEXT(uai::ai::common::LogLevel::kDebug, "lcd: synthetic compose presented; camera remains stopped\n");
        for (;;) {
            tk_dly_tsk(1000);
        }
    }

    UAI_LOG_TEXT(uai::ai::common::LogLevel::kDebug, "camera: driver ready\n");
    status = context.camera.Start();
    if (!status.Ok()) {
        status.LogStatus("camera");
        common::Task::Halt("ai: camera start failed\n");
    }
    UAI_LOG_TEXT(uai::ai::common::LogLevel::kDebug, "camera: start result=ok detail=0\n");
    if constexpr (kLiveCaptureFreezeDiagnostic) {
        uai::ai::pipeline::CaptureFrame first_capture{};
        const std::uint32_t wait_start = common::Task::Now();
        for (;;) {
            status = context.camera.Process();
            if (!status.Ok()) {
                status.LogStatus("camera");
                common::Task::Halt("ai: frozen capture process failed\n");
            }
            uai::ai::pipeline::CaptureFrame candidate{};
            status = context.camera.TakeCompletedCapture(&candidate);
            if (status.Ok()) {
                if ((candidate.sequence % 10U) == 0U) {
                    LogCaptureBrightnessAndSensorExposure(context, candidate);
                    UAI_LOG_DEBUG(
                        "camera: freeze warmup sequence=%u events=%u buffer=%x\n",
                        static_cast<unsigned int>(candidate.sequence),
                        context.camera.GetDiagnostics().frame_event_count,
                        static_cast<unsigned int>(candidate.buffer.address)
                    );
                }
                if (candidate.sequence >= 30U) {
                    first_capture = candidate;
                    break;
                }
                continue;
            }
            if (status.Code() != common::ErrorCode::kNoFrame) {
                status.LogStatus("camera");
                common::Task::Halt("ai: frozen capture acquire failed\n");
            }
            if (static_cast<std::uint32_t>(common::Task::Now() - wait_start) >= 3000U) {
                common::Task::Halt("ai: frozen capture timed out\n");
            }
            tk_dly_tsk(1);
        }
        const unsigned int events_before_stop = context.camera.GetDiagnostics().frame_event_count;
        UAI_LOG_DEBUG(
            "camera: freeze requested sequence=%u address=%x events=%u\n",
            static_cast<unsigned int>(first_capture.sequence),
            static_cast<unsigned int>(first_capture.buffer.address),
            events_before_stop
        );
        status = context.camera.Stop();
        if (!status.Ok()) {
            status.LogStatus("camera");
            common::Task::Halt("ai: camera stop failed; capture not inspected\n");
        }

        uai::ai::pipeline::CaptureFrame latest_capture{};
        const common::Error latest_status = context.camera.TakeCompletedCapture(&latest_capture);
        if (latest_status.Ok()) {
            first_capture = latest_capture;
        } else if (latest_status.Code() != common::ErrorCode::kNoFrame) {
            latest_status.LogStatus("camera");
            common::Task::Halt("ai: stopped capture acquire failed\n");
        }
        status = context.cache.PrepareForCpuRead(first_capture.buffer);
        if (!status.Ok()) {
            status.LogStatus("memory");
            common::Task::Halt("ai: frozen capture cache invalidate failed\n");
        }
        UAI_LOG_DEBUG(
            "camera: freeze complete events_before=%u events_after=%u selected_sequence=%u selected_address=%x\n",
            events_before_stop,
            context.camera.GetDiagnostics().frame_event_count,
            static_cast<unsigned int>(first_capture.sequence),
            static_cast<unsigned int>(first_capture.buffer.address)
        );
        LogFrozenCaptureRegistersAndPixelRows(context, first_capture);

        status = context.lcd.ComposeAndPresent(first_capture, initial, nullptr, true);
        if (!status.Ok()) {
            status.LogStatus("lcd");
            common::Task::Halt("ai: frozen live capture compose failed\n");
        }
        status = context.lcd.SynchronizeCurrentFrame();
        if (!status.Ok()) {
            status.LogStatus("lcd");
            common::Task::Halt("ai: frozen live capture display did not latch\n");
        }
        UAI_LOG_DEBUG(
            "lcd: frozen capture readback fb=%x cr=%x pfcr=%x cfblr=%x cfblnr=%x awcr=%x twcr=%x isr=%x\n",
            static_cast<unsigned int>(LTDC_Layer1->CFBAR),
            static_cast<unsigned int>(LTDC_Layer1->CR),
            static_cast<unsigned int>(LTDC_Layer1->PFCR),
            static_cast<unsigned int>(LTDC_Layer1->CFBLR),
            static_cast<unsigned int>(LTDC_Layer1->CFBLNR),
            static_cast<unsigned int>(LTDC->AWCR),
            static_cast<unsigned int>(LTDC->TWCR),
            static_cast<unsigned int>(LTDC->ISR)
        );
        UAI_LOG_TEXT(uai::ai::common::LogLevel::kDebug, "camera: frozen raw capture displayed; camera stopped\n");
        for (;;) {
            tk_dly_tsk(1000);
        }
    }
    UAI_LOG_INFO(
        "camera: irq dcmipp=%u/%u csi=%u/%u\n",
        static_cast<unsigned int>(NVIC_GetEnableIRQ(DCMIPP_IRQn)),
        static_cast<unsigned int>(NVIC_GetPendingIRQ(DCMIPP_IRQn)),
        static_cast<unsigned int>(NVIC_GetEnableIRQ(CSI_IRQn)),
        static_cast<unsigned int>(NVIC_GetPendingIRQ(CSI_IRQn))
    );
}

}