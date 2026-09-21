#include "person_detector.hpp"

#include <cstddef>
#include <cstdint>

#include <tk/tkernel.h>

extern "C"
{
#include "stm32n6xx_hal.h"
#include <tm/tmonitor.h>
}

namespace person
{

    namespace
    {

        constexpr std::size_t kFrameWidth = 800U;
        constexpr std::size_t kInferenceCropX = 160U;
        constexpr std::size_t kInferenceSize = 480U;

        void trace_npu_state(const char *label)
        {
            const volatile std::uint32_t *npu =
                reinterpret_cast<const volatile std::uint32_t *>(NPU_BASE_S);
            const volatile std::uint32_t *epoch_controller =
                reinterpret_cast<const volatile std::uint32_t *>(NPU_BASE_S + 0x1e000UL);
            tm_printf(reinterpret_cast<const UB *>(
                          "ai: npu %s int=%x ic=%x or=%x and=%x ec=%x addr=%x irq=%x lbl=%x bc=%x\n"),
                      label,
                      static_cast<unsigned int>(npu[0x1008U / 4U]),
                      static_cast<unsigned int>(npu[0x1000U / 4U]),
                      static_cast<unsigned int>(npu[0x1014U / 4U]),
                      static_cast<unsigned int>(npu[0x1024U / 4U]),
                      static_cast<unsigned int>(epoch_controller[0x0U]),
                      static_cast<unsigned int>(epoch_controller[0x8U / 4U]),
                      static_cast<unsigned int>(epoch_controller[0xcU / 4U]),
                      static_cast<unsigned int>(epoch_controller[0x1cU / 4U]),
                      static_cast<unsigned int>(epoch_controller[0x20U / 4U]));
        }

    } // namespace

    bool Detector::Initialize(memory::Manager &memory)
    {
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "ai: runtime init begin\n")));
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "ai: npu hardware ready\n")));

        if (!memory.InitializeModelStorage())
        {
            last_error_ = STAI_ERROR_NETWORK_INVALID_CONTEXT_HANDLE;
            return false;
        }

        HAL_NVIC_SetPriority(NPU0_IRQn, 0U, 0U);
        HAL_NVIC_EnableIRQ(NPU0_IRQn);
        last_error_ = stai_runtime_init();
        tm_printf(reinterpret_cast<const UB *>("ai: runtime init=%x\n"),
                  static_cast<unsigned int>(last_error_));
        if (last_error_ != STAI_SUCCESS)
        {
            return false;
        }
        trace_npu_state("ready");

        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "ai: model init begin\n")));
        last_error_ = model_.init();
        tm_printf(reinterpret_cast<const UB *>("ai: model init=%x\n"),
                  static_cast<unsigned int>(last_error_));
        if (last_error_ != STAI_SUCCESS)
        {
            return false;
        }

        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "ai: model info begin\n")));
        last_error_ = model_.get_info(&info_);
        tm_printf(reinterpret_cast<const UB *>("ai: model info=%x\n"),
                  static_cast<unsigned int>(last_error_));
        if (last_error_ != STAI_SUCCESS || info_.n_inputs != 1U ||
            info_.n_outputs != 3U || info_.inputs == nullptr ||
            info_.outputs == nullptr)
        {
            return false;
        }

        stai_size input_count = 0U;
        stai_size output_count = 0U;
        last_error_ = model_.get_inputs(&input_, &input_count);
        if (last_error_ != STAI_SUCCESS || input_count != 1U || input_ == nullptr)
        {
            return false;
        }
        last_error_ = model_.get_outputs(outputs_, &output_count);
        if (last_error_ != STAI_SUCCESS || output_count != 3U)
        {
            return false;
        }

        if (!inference::initialize(inference::Mode::Person, info_))
        {
            last_error_ = STAI_ERROR_NETWORK_INVALID_INFO;
            return false;
        }

        initialized_ = true;
        last_error_ = STAI_SUCCESS;
        return true;
    }

    void Detector::ConvertCameraToInput(std::uintptr_t camera_frame,
                                        stai_ptr model_input)
    {
        const auto *source = reinterpret_cast<const std::uint16_t *>(camera_frame);
        auto *destination = reinterpret_cast<std::uint8_t *>(model_input);

        /* The model input is the central 480x480 crop in RGB888/U8 NHWC order. */
        for (std::size_t y = 0; y < kInferenceSize; ++y)
        {
            for (std::size_t x = 0; x < kInferenceSize; ++x)
            {
                const std::uint16_t pixel =
                    source[y * kFrameWidth + kInferenceCropX + x];
                const std::uint8_t red = static_cast<std::uint8_t>(
                    ((pixel >> 11U) & 0x1FU) * 255U / 31U);
                const std::uint8_t green = static_cast<std::uint8_t>(
                    ((pixel >> 5U) & 0x3FU) * 255U / 63U);
                const std::uint8_t blue = static_cast<std::uint8_t>(
                    (pixel & 0x1FU) * 255U / 31U);
                const std::size_t offset = (y * kInferenceSize + x) * 3U;
                destination[offset + 0U] = red;
                destination[offset + 1U] = green;
                destination[offset + 2U] = blue;
            }
        }
    }

    bool Detector::Infer(std::uintptr_t camera_frame, memory::Manager &memory,
                         inference::ObjectDetection *detections,
                         std::uint32_t capacity, std::uint32_t *count)
    {
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "ai: infer begin\n")));
        if (!initialized_ || camera_frame == 0U || input_ == nullptr)
        {
            last_error_ = STAI_ERROR_NETWORK_INVALID_CONTEXT_HANDLE;
            return false;
        }

        const memory::Buffer camera_buffer{
            camera_frame, 800U * 480U * 2U, memory::Region::kExternalPsram};
        memory.PrepareForCpuRead(camera_buffer);
        ConvertCameraToInput(camera_frame, input_);

        const memory::Buffer input_buffer{
            reinterpret_cast<std::uintptr_t>(input_), info_.inputs[0].size_bytes,
            memory::Region::kInternalMedia};
        memory.PrepareForDisplayRead(input_buffer);

        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "ai: infer run begin\n")));
        trace_npu_state("before");
        last_error_ = model_.run(STAI_MODE_ASYNC);
        if (last_error_ >= STAI_ERROR_GENERIC)
        {
            return false;
        }

        constexpr std::uint32_t kInferenceTimeoutTicks = 5000U;
        bool completed = false;
        for (std::uint32_t tick = 0; tick < kInferenceTimeoutTicks; ++tick)
        {
            last_error_ = model_.get_run_status();
            if (last_error_ == STAI_DONE)
            {
                completed = true;
                break;
            }
            if (last_error_ >= STAI_ERROR_GENERIC)
            {
                return false;
            }
            if ((tick % 100U) == 0U)
            {
                trace_npu_state("wait");
            }
            if (last_error_ == STAI_RUNNING_WFE)
            {
                (void)model_.wfe();
            }
            (void)model_.run_continue();
            if (last_error_ != STAI_RUNNING_WFE)
            {
                tk_dly_tsk(1);
            }
        }
        if (!completed)
        {
            last_error_ = STAI_ERROR_NETWORK_INVALID_RUN;
            trace_npu_state("timeout");
            return false;
        }

        tm_printf(reinterpret_cast<const UB *>("ai: infer run=%x\n"),
                  static_cast<unsigned int>(STAI_SUCCESS));

        for (std::uint16_t i = 0; i < info_.n_outputs; ++i)
        {
            const memory::Buffer output_buffer{
                reinterpret_cast<std::uintptr_t>(outputs_[i]),
                info_.outputs[i].size_bytes, memory::Region::kInternalMedia};
            memory.PrepareForCpuRead(output_buffer);
        }

        if (!inference::process_person(info_, outputs_, detections, capacity,
                                       count))
        {
            last_error_ = STAI_ERROR_NETWORK_INVALID_OUT_PTR;
            return false;
        }

        last_error_ = model_.new_inference();
        if (last_error_ != STAI_SUCCESS)
        {
            return false;
        }
        return true;
    }

} // namespace person
