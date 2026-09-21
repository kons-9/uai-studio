#include <cstddef>
#include <cstdint>
#include <cstring>

#include <tk/tkernel.h>

#include "driver/display_driver.hpp"
#include "memory_manager.hpp"
#include "person_detector.hpp"

extern "C"
{
#include <tm/tmonitor.h>

#include "stm32n6570_discovery_camera.h"
#include "stm32n6570_discovery_lcd.h"
#include "stm32n6xx_hal.h"

    extern DCMIPP_HandleTypeDef hcamera_dcmipp;
}

namespace
{

    constexpr std::size_t kFrameBytes = 800U * 480U * 2U;
    constexpr std::size_t kFrameWidth = 800U;
    constexpr std::size_t kFrameHeight = 480U;
    constexpr std::size_t kInferenceCropX = 160U;
    constexpr std::size_t kInferenceSize = 480U;
    constexpr std::size_t kLcdBufferCount = 2U;
    constexpr TMO kDisplayHoldTicks = 20;
    constexpr std::size_t kMaxDetections = 100U;

    /* usermain is the μT-Kernel initial task and has only a 1 KiB stack. */
    static inference::ObjectDetection detection_results[kMaxDetections] = {};

    volatile std::uintptr_t completed_camera_frame = 0;
    volatile std::uint32_t camera_frame_events = 0;
    std::uintptr_t next_camera_frame = 0;
    std::uintptr_t camera_frame0 = 0;
    std::uintptr_t camera_frame1 = 0;
    memory::Buffer lcd_frames[kLcdBufferCount]{};
    std::size_t next_lcd_buffer = 0U;

    int clamp_coordinate(float value, int maximum)
    {
        if (value <= 0.0f)
        {
            return 0;
        }
        if (value >= static_cast<float>(maximum))
        {
            return maximum;
        }
        return static_cast<int>(value);
    }

    int normalized_to_screen_x(float value)
    {
        return static_cast<int>(kInferenceCropX) +
               clamp_coordinate(value * static_cast<float>(kInferenceSize),
                                static_cast<int>(kInferenceSize - 1U));
    }

    int normalized_to_screen_y(float value)
    {
        return clamp_coordinate(value * static_cast<float>(kInferenceSize),
                                static_cast<int>(kFrameHeight - 1U));
    }

    void draw_pixel(const memory::Buffer &lcd_frame, int x, int y,
                    std::uint16_t color)
    {
        if (x < 0 || y < 0 || x >= static_cast<int>(kFrameWidth) ||
            y >= static_cast<int>(kFrameHeight))
        {
            return;
        }
        auto *pixels = reinterpret_cast<std::uint16_t *>(lcd_frame.address);
        pixels[static_cast<std::size_t>(y) * kFrameWidth +
               static_cast<std::size_t>(x)] = color;
    }

    void draw_box(const memory::Buffer &lcd_frame,
                  const inference::ObjectDetection &detection)
    {
        const int x0 = normalized_to_screen_x(
            detection.x_center - detection.width * 0.5f);
        const int y0 = normalized_to_screen_y(
            detection.y_center - detection.height * 0.5f);
        const int x1 = normalized_to_screen_x(
            detection.x_center + detection.width * 0.5f);
        const int y1 = normalized_to_screen_y(
            detection.y_center + detection.height * 0.5f);
        constexpr int kThickness = 5;
        constexpr std::uint16_t kRedRgb565 = 0xF800U;

        for (int offset = 0; offset < kThickness; ++offset)
        {
            for (int x = x0; x <= x1; ++x)
            {
                draw_pixel(lcd_frame, x, y0 + offset, kRedRgb565);
                draw_pixel(lcd_frame, x, y1 - offset, kRedRgb565);
            }
            for (int y = y0; y <= y1; ++y)
            {
                draw_pixel(lcd_frame, x0 + offset, y, kRedRgb565);
                draw_pixel(lcd_frame, x1 - offset, y, kRedRgb565);
            }
        }
    }

    void draw_detections(const memory::Buffer &lcd_frame,
                         const inference::ObjectDetection *detections,
                         std::uint32_t count)
    {
        for (std::uint32_t i = 0; i < count; ++i)
        {
            draw_box(lcd_frame, detections[i]);
        }
    }

    [[noreturn]] void halt_with_message(const char *message)
    {
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(message)));
        for (;;)
        {
            tk_dly_tsk(1000);
        }
    }

    bool stage_camera_frame(std::uintptr_t frame,
                            memory::Manager &memory,
                            memory::Buffer &lcd_frame)
    {
        if (frame == 0U)
        {
            return false;
        }

        lcd_frame = lcd_frames[next_lcd_buffer];
        next_lcd_buffer = (next_lcd_buffer + 1U) % kLcdBufferCount;
        if (!lcd_frame)
        {
            return false;
        }

        /* The camera writes PSRAM directly. Invalidate before the CPU reads it,
         * then stage this complete frame in an independent LCD buffer. */
        const memory::Buffer camera_frame{
            frame, kFrameBytes, memory::Region::kExternalPsram};
        memory.PrepareForCpuRead(camera_frame);
        std::memcpy(reinterpret_cast<void *>(lcd_frame.address),
                    reinterpret_cast<const void *>(frame), kFrameBytes);
        return true;
    }

    bool submit_lcd_frame(
        const memory::Buffer &lcd_frame,
        memory::Manager &memory,
        const inference::ObjectDetection *detections,
        std::uint32_t detection_count)
    {
        if (!lcd_frame)
        {
            return false;
        }
        if (detections != nullptr)
        {
            draw_detections(lcd_frame, detections, detection_count);
        }
        memory.PrepareForDisplayRead(lcd_frame);

        const bool submitted =
            BSP_LCD_Reload(0, BSP_LCD_RELOAD_NONE) == BSP_ERROR_NONE &&
            BSP_LCD_SetLayerAddress(0, 0,
                                    static_cast<uint32_t>(lcd_frame.address)) ==
                BSP_ERROR_NONE &&
            BSP_LCD_SetLayerVisible(0, 0, ENABLE) == BSP_ERROR_NONE &&
            BSP_LCD_Reload(0, BSP_LCD_RELOAD_VERTICAL_BLANKING) ==
                BSP_ERROR_NONE;
        if (submitted)
        {
            /* Let the vertical-blanking reload complete before this buffer can be
             * selected again. This adds a small latency but prevents the CPU from
             * writing a buffer while LTDC is scanning it. */
            tk_dly_tsk(kDisplayHoldTicks);
        }
        return submitted;
    }

} // namespace

extern "C" void BSP_CAMERA_FrameEventCallback(uint32_t Instance)
{
    (void)Instance;
    ++camera_frame_events;
    completed_camera_frame = next_camera_frame;
    next_camera_frame = (next_camera_frame == camera_frame0)
                            ? camera_frame1
                            : camera_frame0;
}

extern "C" void DCMIPP_IRQHandler(void)
{
    HAL_DCMIPP_IRQHandler(&hcamera_dcmipp);
}

extern "C" void CSI_IRQHandler(void)
{
    HAL_DCMIPP_CSI_IRQHandler(&hcamera_dcmipp);
}

extern "C" void IAC_IRQHandler(void)
{
    const uint32_t flags0 = IAC->ISR[0];
    const uint32_t flags1 = IAC->ISR[1];
    const uint32_t flags2 = IAC->ISR[2];
    const uint32_t flags3 = IAC->ISR[3];
    const uint32_t flags4 = IAC->ISR[4];
    const uint32_t flags5 = IAC->ISR[5];
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: IAC flags=%x,%x,%x,%x,%x,%x\n"),
              static_cast<unsigned int>(flags0),
              static_cast<unsigned int>(flags1),
              static_cast<unsigned int>(flags2),
              static_cast<unsigned int>(flags3),
              static_cast<unsigned int>(flags4),
              static_cast<unsigned int>(flags5));
    if ((flags4 & 0x00400000U) != 0U)
    {
        /* IAC register 4 bit 22 is RISAF12 (XSPI2).  Capture the
         * transaction metadata before HAL_RIF_IRQHandler clears the source. */
        tm_printf(reinterpret_cast<const UB *>(
                      "ai: RISAF12 iasr=%x iaesr=%x iaddr=%x\n"),
                  static_cast<unsigned int>(RISAF12->IASR),
                  static_cast<unsigned int>(RISAF12->IAR->IAESR),
                  static_cast<unsigned int>(RISAF12->IAR->IADDR));
    }
    HAL_RIF_IRQHandler();
}

extern "C" INT usermain(void)
{
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "camera: external-PSRAM camera test\n")));

    memory::Manager memory;
    if (!memory.Initialize(true))
    {
        halt_with_message("memory: PSRAM initialization failed\n");
    }

    uai::driver::DisplayDriver display;
    if (!uai::driver::IsOk(display.Initialize()))
    {
        halt_with_message("display: initialization failed\n");
    }

    const auto camera_buffer0 = memory.Allocate(
        memory::Region::kExternalPsram, kFrameBytes);
    const auto camera_buffer1 = memory.Allocate(
        memory::Region::kExternalPsram, kFrameBytes);
    lcd_frames[0] = memory.Allocate(
        memory::Region::kExternalPsram, kFrameBytes, 0x00100000U);
    lcd_frames[1] = memory.Allocate(
        memory::Region::kExternalPsram, kFrameBytes, 0x00100000U);
    if (!camera_buffer0 || !camera_buffer1 || !lcd_frames[0] ||
        !lcd_frames[1])
    {
        halt_with_message("memory: camera buffer allocation failed\n");
    }

    camera_frame0 = camera_buffer0.address;
    camera_frame1 = camera_buffer1.address;
    memory.PrepareForDmaWrite(camera_buffer0);
    memory.PrepareForDmaWrite(camera_buffer1);

    if (BSP_CAMERA_Init(0, CAMERA_R2592x1944, CAMERA_PF_RAW_RGGB10) !=
        BSP_ERROR_NONE)
    {
        halt_with_message("camera: initialization failed\n");
    }

    memory.KeepInferenceClocksOnSleep();

    completed_camera_frame = 0;
    next_camera_frame = camera_frame0;
    if (BSP_CAMERA_DoubleBufferStart(
            0, reinterpret_cast<uint8_t *>(camera_frame0),
            reinterpret_cast<uint8_t *>(camera_frame1),
            CAMERA_MODE_CONTINUOUS) != BSP_ERROR_NONE)
    {
        halt_with_message("camera: start failed\n");
    }

    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "camera: preview started\n")));

    person::Detector detector;
    enum class AiState
    {
        kShowCameraFirst,
        kInitialize,
        kReady,
        kCameraOnly,
    };
    AiState ai_state = AiState::kShowCameraFirst;

    for (;;)
    {
        if (BSP_CAMERA_BackgroundProcess() != BSP_ERROR_NONE)
        {
            halt_with_message("camera: background process failed\n");
        }

        const std::uintptr_t frame = completed_camera_frame;
        completed_camera_frame = 0;
        if (frame == 0U)
        {
            tk_dly_tsk(1);
            continue;
        }

        if (camera_frame_events <= 3U)
        {
            tm_printf(reinterpret_cast<const UB *>(
                          "camera: frame event %u\n"),
                      static_cast<unsigned int>(camera_frame_events));
        }

        /* Stage one complete camera frame before touching the AI runtime.
         * This keeps the display usable even when model loading or inference
         * fails, and gives inference a stable copy that the camera cannot
         * overwrite. */
        if (ai_state == AiState::kShowCameraFirst)
        {
            memory::Buffer lcd_frame;
            if (!stage_camera_frame(frame, memory, lcd_frame) ||
                !submit_lcd_frame(lcd_frame, memory, nullptr, 0U))
            {
                halt_with_message("display: process failed\n");
            }
            tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
                "camera: preview visible\n")));
            ai_state = AiState::kInitialize;
            tk_dly_tsk(1);
            continue;
        }

        memory::Buffer lcd_frame;
        if (!stage_camera_frame(frame, memory, lcd_frame))
        {
            halt_with_message("camera: frame staging failed\n");
        }

        if (ai_state == AiState::kInitialize)
        {
            /* Show this frame before NOR/model initialization, so an AI-side
             * fault cannot leave the display showing only the first frame. */
            if (!submit_lcd_frame(lcd_frame, memory, nullptr, 0U))
            {
                halt_with_message("display: process failed\n");
            }
            tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
                "ai: initialize begin\n")));
            const bool model_ready = detector.Initialize(memory);
            tm_printf(reinterpret_cast<const UB *>(
                          "ai: initialize returned=%u error=%x\n"),
                      model_ready ? 1U : 0U,
                      static_cast<unsigned int>(detector.LastError()));
            if (model_ready)
            {
                ai_state = AiState::kReady;
                tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
                    "ai: person model ready\n")));
            }
            else
            {
                ai_state = AiState::kCameraOnly;
                tm_printf(reinterpret_cast<const UB *>(
                              "ai: person model init error=%x; camera-only\n"),
                          static_cast<unsigned int>(detector.LastError()));
                tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
                    "ai: person model unavailable; camera-only\n")));
            }
        }

        std::uint32_t detection_count = 0U;
        if (ai_state == AiState::kReady &&
            !detector.Infer(lcd_frame.address, memory, detection_results,
                            kMaxDetections,
                            &detection_count))
        {
            ai_state = AiState::kCameraOnly;
            tm_printf(reinterpret_cast<const UB *>(
                          "ai: person model inference error=%x; camera-only\n"),
                      static_cast<unsigned int>(detector.LastError()));
            tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
                "ai: person model inference failed; camera-only\n")));
            detection_count = 0U;
        }

        static std::uint32_t inference_frames = 0U;
        if (ai_state == AiState::kReady)
        {
            ++inference_frames;
            if ((inference_frames % 10U) == 0U)
            {
                tm_printf(reinterpret_cast<const UB *>(
                              "ai: infer frame=%u detections=%u\n"),
                          static_cast<unsigned int>(inference_frames),
                          static_cast<unsigned int>(detection_count));
            }
        }

        if (!submit_lcd_frame(lcd_frame, memory, detection_results,
                              detection_count))
        {
            halt_with_message("display: process failed\n");
        }
        tk_dly_tsk(1);
    }
}
